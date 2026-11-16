#include "serial_link.h"
#include "gba.h"
#include "include_asm.h"

/* COMMON: ordinary tentative C definitions, in original address order.
 * No separate private .lcomm objects or ROM tables are identified here.
 * Packet constants are immediates; the IRQ table belongs to system. */
struct MultiSioArea gMultiSioArea;               /* @address 0x03004DF0; 0x130 bytes. */
struct SerialLinkSession gSerialLinkSession;     /* @address 0x03004F20; 0x60 bytes. */
struct SerialLinkTransfer gSerialLinkTransfer;   /* @address 0x03004F80; 0x10 bytes. */

/**
 * @brief 配置 115200 bps 多人串口并重置底层收发状态
 * @note 清 0x130 字节，设置发送双缓冲和接收三组双玩家缓冲；索引初值均为 16。
 *       先禁用 SIO/Timer3 IRQ，再只启用 SIO IRQ；硬件位含义见官方 gba/define.h。
 * @address 0x08016C88
 */
void MultiSioInit(void)
{
    u32 clearValue;

    (*(vu16 *)REG_IME) = 0;
    (*(vu16 *)REG_IE) &= ~(SIO_INTR_FLAG | MULTI_SIO_TIMER_INTR_FLAG);
    (*(vu16 *)REG_IME) = 1;
    (*(vu16 *)REG_RCNT) = R_SIO_MASTER_MODE;
    (*(vu16 *)REG_SIOCNT) = SIO_MULTI_MODE;
    (*(vu16 *)REG_SIOCNT) |= SIO_IF_ENABLE | MULTI_SIO_BAUD_RATE_NO;
    clearValue = 0;
    CpuSet(&clearValue, &gMultiSioArea,
        DMA_SRC_FIX | DMA_32BIT_BUS | (sizeof(gMultiSioArea) / 4));
    gMultiSioArea.sendBufCounter = sizeof(struct MultiSioPacket) / 2;
    gMultiSioArea.recvBufCounter = sizeof(struct MultiSioPacket) / 2;
    gMultiSioArea.nextSendBufp = (u16 *)&gMultiSioArea.sendBuf[0];
    gMultiSioArea.currentSendBufp = (u16 *)&gMultiSioArea.sendBuf[1];
    gMultiSioArea.currentRecvBufp = (u16 *)&gMultiSioArea.recvBuf[0];
    gMultiSioArea.lastRecvBufp = (u16 *)&gMultiSioArea.recvBuf[1];
    gMultiSioArea.recvCheckBufp = (u16 *)&gMultiSioArea.recvBuf[2];
    (*(vu16 *)REG_IME) = 0;
    (*(vu16 *)REG_IE) |= SIO_INTR_FLAG;
    (*(vu16 *)REG_IME) = 1;
}

/**
 * @brief 探测主从角色、取得消息并返回组合状态
 * @note recvp 指向两个连续的 24 字节消息；有效位在低四位，历史玩家位在 8..11。
 *       主机切换到 Timer3 IRQ；预热计数达到 8 后的下一次调用进入 established。
 * @address 0x08016D24
 */
INCLUDE_ASM("asm/nonmatchings/serial_link", MultiSioMain);

/**
 * @brief 将 24 字节消息封装为待发送的串口包
 * @note 源需四字节对齐；复制六个 word。写序号和缺失玩家位，再令前 14 个半字之和为 0xFFEF。
 * @address 0x08016E30
 */
void MultiSioSendDataSet(const void *sendp)
{
    s32 checkSum = 0;
    s32 i;

    ((struct MultiSioPacket *)gMultiSioArea.nextSendBufp)->frameCounter = gMultiSioArea.sendFrameCounter;
    ((struct MultiSioPacket *)gMultiSioArea.nextSendBufp)->recvErrorFlags =
        gMultiSioArea.connectedFlags ^ gMultiSioArea.recvSuccessFlags;
    ((struct MultiSioPacket *)gMultiSioArea.nextSendBufp)->checkSum = 0;
    CpuCopy(sendp, &gMultiSioArea.nextSendBufp[2], MULTI_SIO_BLOCK_SIZE, 32);
    for (i = 0; i < sizeof(struct MultiSioPacket) / 2 - 2; i++)
        checkSum += gMultiSioArea.nextSendBufp[i];
    ((struct MultiSioPacket *)gMultiSioArea.nextSendBufp)->checkSum = ~checkSum - 16;
    gMultiSioArea.transmitPending = 1;
}

/**
 * @brief 取得已发布的接收缓冲，校验并复制两端消息
 * @note IRQ 屏蔽期间交换 ready/read 指针并消费 receivePending；返回本次有效玩家位。
 *       错误包不改输出；处理过的槽只清消息的 24 字节，头和尾部仍保留。
 * @address 0x08016E80
 */
u32 MultiSioRecvDataCheck(void *recvp)
{
    u16 *bufp;
    u32 recvCheck;
    s32 checkSum;
    s32 i, ii;
    u32 clearValue;

    (*(vu16 *)REG_IME) = 0;
    bufp = gMultiSioArea.recvCheckBufp;
    gMultiSioArea.recvCheckBufp = gMultiSioArea.lastRecvBufp;
    gMultiSioArea.lastRecvBufp = bufp;
    recvCheck = gMultiSioArea.receivePending;
    gMultiSioArea.receivePending = 0;
    (*(vu16 *)REG_IME) = 1;
    gMultiSioArea.recvSuccessFlags = 0;
    if (recvCheck)
    {
        for (i = 0; i < MULTI_SIO_PLAYERS_MAX; i++)
        {
            bufp = gMultiSioArea.recvCheckBufp + i * (sizeof(struct MultiSioPacket) / 2);
            checkSum = 0;
            for (ii = 0; ii < sizeof(struct MultiSioPacket) / 2 - 2; ii++)
                checkSum += bufp[ii];
            if ((s16)checkSum == -17)
            {
                CpuCopy(bufp + 2, (u8 *)recvp + i * MULTI_SIO_BLOCK_SIZE,
                    MULTI_SIO_BLOCK_SIZE, 32);
                gMultiSioArea.recvSuccessFlags |= 1 << i;
            }
            clearValue = 0;
            CpuSet(&clearValue, bufp + 2,
                DMA_SRC_FIX | DMA_32BIT_BUS | (MULTI_SIO_BLOCK_SIZE / 4));
        }
    }
    gMultiSioArea.connectedFlags |= gMultiSioArea.recvSuccessFlags;
    return gMultiSioArea.recvSuccessFlags;
}

/**
 * @brief 在 VBlank 启动主机串口帧，或补足从机等待唤醒
 * @note 主机发布待发包、轮换接收缓冲并发送 0xFEFE，同步索引置 -1，启动 Timer3。
 *       从机本帧未见 IRQ 时置 BIOS 的 SIO 等待标志，随后清 interruptSeen。
 * @address 0x08016F30
 */
INCLUDE_ASM("asm/nonmatchings/serial_link", SerialLink_ServiceVBlank);

/**
 * @brief 处理 SIO/Timer3 中断中的逐半字收发
 * @note 读取四个硬件接收槽，只保存玩家 0/1；接收半字 13 时发布完成标志。
 *       玩家 0 的 0xFEFE 在 recvBufCounter > 13 时触发同步与换缓冲；主机续启 Timer3。
 * @address 0x08016FC0
 */
INCLUDE_ASM("asm/nonmatchings/serial_link", MultiSioIntr);

/**
 * @brief 允许已识别主机在 VBlank 发起传输
 * @note 仅 type 非零时设置 startFlag；从机调用不改变它。
 * @address 0x080170BC
 */
void MultiSioStart(void)
{
    if (gMultiSioArea.type)
        gMultiSioArea.startFlag = 1;
}

/**
 * @brief 停止底层 SIO/Timer3 传输
 * @note 禁用两个 IRQ，恢复 SIOCNT=0x2003 和 Timer3 reload=0xBFC0，清主机传输许可；保留缓冲。
 * @address 0x080170D0
 */
void MultiSioStop(void)
{
    (*(vu16 *)REG_IME) = 0;
    (*(vu16 *)REG_IE) &= ~(SIO_INTR_FLAG | MULTI_SIO_TIMER_INTR_FLAG);
    (*(vu16 *)REG_IME) = 1;
    (*(vu16 *)REG_SIOCNT) = SIO_MULTI_MODE | MULTI_SIO_BAUD_RATE_NO;
    (*(vu32 *)REG_TM3CNT) = MULTI_SIO_TIMER_COUNT;
    (*(vu16 *)REG_IF) = SIO_INTR_FLAG | MULTI_SIO_TIMER_INTR_FLAG;
    gMultiSioArea.startFlag = 0;
}

/**
 * @brief 收取协议消息、更新签名并排队下一包
 * @note 旧状态显示从机且已见玩家 0 时等待 SIO；enableMaster 非零时开启主机发送。
 *       握手开始后按 SIO 错误、玩家 ID 超出 0/1、预热后缺包顺序返回 1/2/3；正常为 0。
 * @address 0x08017120
 */
INCLUDE_ASM("asm/nonmatchings/serial_link", SerialLink_ExchangePackets);

/**
 * @brief 推进签名握手与双向 16 字节块传输
 * @note 返回阶段 0/1/2，完成为 -1，错误为 0x101..0x104。十次未见签名时重新初始化。
 *       请求块去重；响应先复制后校验，仅有效块推进序号。活动超过上次进展 60 次 poll 后超时。
 *       双方完成位和确认位满足时停止；完成确认后遇到 SIO/缺包错误也可按成功收尾。
 * @address 0x080171E4
 */
INCLUDE_ASM("asm/nonmatchings/serial_link", SerialLink_PollBlockTransfer);

/**
 * @brief 查询预热完成且对端消息仍保有协议签名
 * @note 要求 gMultiSioArea.state==2 和 selectedPeer.signature==0x4E4C；返回 0/1，不发起通信。
 * @address 0x08017588
 */
s32 SerialLink_HasPeerSignature(void)
{
    s32 result = 0;
    if (gMultiSioArea.state == SERIAL_LINK_TRANSPORT_ESTABLISHED)
    {
        u32 offset = gSerialLinkSession.peerIndex * sizeof(struct SerialLinkMessage);
        struct SerialLinkMessage *recvp = gSerialLinkSession.received;
        if (((struct SerialLinkMessage *)(offset + (u32)recvp))->signature == SERIAL_LINK_SIGNATURE)
            result = 1;
    }
    return result;
}

/**
 * @brief 重置底层与 0x60 字节会话，并执行首次包交换
 * @note 等价于分块初始化的公共起始步骤；保留独立 transfer 参数，不配置块数和自动启动。
 * @address 0x080175C0
 */
void SerialLink_ResetSession(void)
{
    u32 clearValue;
    s32 i;

    MultiSioInit();
    clearValue = 0;
    CpuSet(&clearValue, &gSerialLinkSession,
        DMA_SRC_FIX | DMA_32BIT_BUS | (sizeof(gSerialLinkSession) / 4));
    for (i = 0; i < MULTI_SIO_PLAYERS_MAX; i++)
    {
        gSerialLinkSession.received[i].signature = 0;
        gSerialLinkSession.received[i].command = 0;
    }
    SerialLink_ExchangePackets(1);
}

/**
 * @brief 保存收发缓冲、传输长度和自动启动参数
 * @note 参数顺序为 receive、send；长度右移 4 后存 u16，活动会话再取块数低八位。
 *       无长度、缓冲边界校验；autoStart 以 u16 保存，会话只保留低八位。此函数不碰硬件。
 * @address 0x08017600
 */
void SerialLink_ConfigureTransfer(void *receiveData, const void *sendData,
    u32 byteSize, u16 autoStart)
{
    gSerialLinkTransfer.receiveData = receiveData;
    gSerialLinkTransfer.sendData = sendData;
    gSerialLinkTransfer.blockCount = byteSize >> 4;
    gSerialLinkTransfer.phase = SERIAL_LINK_TRANSFER_INITIALIZING;
    gSerialLinkTransfer.autoStart = autoStart;
}

/**
 * @brief 撤销对端签名并停止通信
 * @note 只清当前 peerIndex 的签名，再调用 MultiSioStop；保留阶段、参数和已接收数据。
 * @address 0x0801761C
 */
void SerialLink_Close(void)
{
    u32 offset = gSerialLinkSession.peerIndex * sizeof(struct SerialLinkMessage);
    struct SerialLinkMessage *recvp = gSerialLinkSession.received;
    ((struct SerialLinkMessage *)(offset + (u32)recvp))->signature = 0;
    MultiSioStop();
}

/**
 * @brief 按 word 数复制数据，兼容未对齐的源和目标
 * @note 两端均四字节对齐时用 word 访问，否则按 byte 复制 wordCount*4 字节；零长度不写入。
 *       顺序为 destination、source；重叠区域没有 memmove 语义。
 * @address 0x08017640
 */
INCLUDE_ASM("asm/nonmatchings/serial_link", SerialLink_CopyWords);
