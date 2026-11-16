#ifndef SERIAL_LINK_H
#define SERIAL_LINK_H

#include "common.h"
#include "gba/define.h"

/* MultiSio sample names are retained for the common transport interfaces.
 * Related SDK-derived version: SAT-R/sa2, include/multi_sio.h and src/multi_sio.c,
 * commit 393357f7c9d46b5c10b6c41a57b80fe3b58f3c8d.
 * This ROM uses a two-player layout, 24-byte blocks and checksum -17;
 * the later four-player/load-extension layout must not be substituted.
 * SerialLinkMessage/Session/Transfer describe the game's additional protocol,
 * not the incompatible MultiBootParam from docs/include/AgbMultiBoot.h. */

#define MULTI_SIO_PLAYERS_MAX 2
#define SERIAL_LINK_BLOCK_BYTES 16
#define SERIAL_LINK_BLOCK_WORDS 4
#define MULTI_SIO_BLOCK_SIZE 24
#define SERIAL_LINK_PACKET_HALFWORDS 14
#define SERIAL_LINK_PACKET_BUFFER_BYTES 32
#define MULTI_SIO_SYNC_DATA 0xFEFE
#define SERIAL_LINK_CHECKSUM_RESIDUE 0xFFEF
#define SERIAL_LINK_SIGNATURE 0x4E4C
#define MULTI_SIO_TIMER_COUNT 0xBFC0
#define MULTI_SIO_BAUD_RATE 115200
#define MULTI_SIO_BAUD_RATE_NO SIO_115200_BPS
#define MULTI_SIO_TIMER_NO 3
#define MULTI_SIO_TIMER_INTR_FLAG TIMER3_INTR_FLAG
#define SERIAL_LINK_WARMUP_POLLS 8
#define SERIAL_LINK_HANDSHAKE_RETRY_POLLS 10
#define SERIAL_LINK_PROGRESS_TIMEOUT_POLLS 60

enum SerialLinkTransportPhase
{
    SERIAL_LINK_TRANSPORT_DISCOVERING = 0,
    SERIAL_LINK_TRANSPORT_WARMUP = 1,
    SERIAL_LINK_TRANSPORT_ESTABLISHED = 2
};

/* SDK MultiSioMain return flags. */
#define MULTI_SIO_RECV_ID_MASK 0x000F
#define MULTI_SIO_CONNECTED_ID_MASK 0x0F00
#define MULTI_SIO_RECV_ID_SHIFT 0
#define MULTI_SIO_CONNECTED_ID_SHIFT 8
#define MULTI_SIO_RECV_ID0 0x0001
#define MULTI_SIO_RECV_ID1 0x0002
#define MULTI_SIO_TYPE 0x0080
#define MULTI_SIO_PARENT 0x0080
#define MULTI_SIO_CHILD 0x0000
#define MULTI_SIO_CONNECTED_ID0 0x0100
#define MULTI_SIO_CONNECTED_ID1 0x0200
#define MULTI_SIO_HARD_ERROR 0x1000
#define MULTI_SIO_ID_OVER_ERROR 0x2000
#define MULTI_SIO_RECV_FLAGS_AVAILABLE 0x8000

enum SerialLinkPacketResult
{
    SERIAL_LINK_PACKET_OK = 0,
    SERIAL_LINK_PACKET_SIO_ERROR = 1,
    SERIAL_LINK_PACKET_UNSUPPORTED_PLAYER = 2,
    SERIAL_LINK_PACKET_MISSING_PLAYER = 3
};

enum SerialLinkTransferPhase
{
    SERIAL_LINK_TRANSFER_INITIALIZING = 0,
    SERIAL_LINK_TRANSFER_HANDSHAKE = 1,
    SERIAL_LINK_TRANSFER_ACTIVE = 2
};

/* PollBlockTransfer otherwise returns its current SerialLinkTransferPhase. */
enum SerialLinkTransferResult
{
    SERIAL_LINK_TRANSFER_COMPLETE = -1,
    SERIAL_LINK_TRANSFER_SIO_ERROR = 0x101,
    SERIAL_LINK_TRANSFER_UNSUPPORTED_PLAYER = 0x102,
    SERIAL_LINK_TRANSFER_MISSING_PLAYER = 0x103,
    SERIAL_LINK_TRANSFER_TIMEOUT = 0x104
};

enum SerialLinkCommand
{
    SERIAL_LINK_COMMAND_MASK = 0x001F,
    SERIAL_LINK_COMMAND_REQUEST_BLOCK = 0x0001,
    SERIAL_LINK_COMMAND_ACK_COMPLETE = 0x0020,
    SERIAL_LINK_COMMAND_RECEIVE_COMPLETE = 0x0040,
    SERIAL_LINK_COMMAND_HAS_BLOCK = 0x0080,
    SERIAL_LINK_COMMAND_REQUEST_SHIFT = 8
};

/**
 * @brief 单次响应中的 16 字节数据块
 * @note CopyWords 以四个 word 复制，块校验和按 16 个 byte 累加；两种视图共享存储。
 * @address 消息内 +0x04；不是独立 ROM/RAM 对象。
 */
union SerialLinkBlock
{
    u8 bytes[SERIAL_LINK_BLOCK_BYTES];
    u32 words[SERIAL_LINK_BLOCK_WORDS];
};

/**
 * @brief 上层握手、块请求和块响应共用的 24 字节消息
 * @note command 高字节请求块号；blockTag 低字节为响应块号，高字节为块字节和。
 * @address 0x03004F20 / 0x03004F38 / 0x03004F50；也嵌于底层收发包 +0x04。
 */
struct SerialLinkMessage
{
    u16 signature;                  /* +0x00; SERIAL_LINK_SIGNATURE 表示有效协议消息。 */
    u16 command;                    /* +0x02; SerialLinkCommand 标志及请求块号。 */
    union SerialLinkBlock block;    /* +0x04; 可与反向块请求同时携带。 */
    u16 blockTag;                   /* +0x14; (sum(block.bytes) << 8) | blockIndex。 */
    u16 reserved16;                 /* +0x16; 被整包复制/清零，尚无独立消费者。 */
};

/**
 * @brief 底层串口帧的 32 字节缓冲槽
 * @note 前 14 个半字之和模 65536 为 0xFFEF；末两字可被接收 IRQ 写入，不参与校验。
 * @address transport +0x30 起；发送双缓冲，接收三组双玩家缓冲。
 */
struct MultiSioPacket
{
    u8 frameCounter;                 /* +0x00; MultiSioMain 的递增计数。 */
    u8 recvErrorFlags;               /* +0x01; connectedFlags ^ recvSuccessFlags。 */
    u16 checkSum;                    /* +0x02; -17 - 其余 13 个半字之和。 */
    u16 data[MULTI_SIO_BLOCK_SIZE / 2]; /* +0x04; SDK 的原始半字视图，承载 SerialLinkMessage。 */
    u16 overrunCatch[2];             /* +0x1C; 接收索引 14/15 的尾部空间。 */
};

/**
 * @brief 两端多人串口的状态、缓冲指针和包存储
 * @note 使用 SIO_MULTI_MODE / SIO_115200_BPS；布局由 Init、收发及 ARM 换缓冲入口共同确定。
 * @address 0x03004DF0，大小 0x130。
 */
struct MultiSioArea
{
    u8 type;                        /* +0x00; SIO_MULTI_PARENT / SIO_MULTI_CHILD，主机为 8。 */
    u8 state;                       /* +0x01; SerialLinkTransportPhase。 */
    u8 connectedFlags;              /* +0x02; 曾通过校验的玩家位，按 OR 累积。 */
    u8 recvSuccessFlags;            /* +0x03; 本次 MultiSioRecvDataCheck 的有效玩家位。 */
    u8 transmitPending;             /* +0x04; 本作变体：SendDataSet 置位，发布发送缓冲时清除。 */
    u8 receivePending;              /* +0x05; 本作变体：收到半字 13 置位，交换读取缓冲时清除。 */
    u8 startFlag;                   /* +0x06; 允许 VBlank 主机发起下一帧。 */
    u8 hardError;                   /* +0x07; 最近 SIOCNT bit 6 的快照。 */
    u8 recvFlagsAvailableCounter;   /* +0x08; 收到首包后饱和到 8，下一次 poll 进入 state 2。 */
    u8 interruptSeen;               /* +0x09; 本作变体：IRQ 置位，从机 VBlank 消费。 */
    u8 reserved0A;                  /* +0x0A; 仅见初始化清零。 */
    u8 sendFrameCounter;            /* +0x0B; 每次 MultiSioMain 加一，按字节回绕。 */
    u8 recvFrameCounter[4][2];       /* +0x0C; 沿用 SDK 保留区视图，本作仅见清零。 */
    s32 sendBufCounter;             /* +0x14; 初始化 16；发布后 0..15。 */
    s32 recvBufCounter;             /* +0x18; 初始化 16；同步字后 -1，再逐半字递增到 15。 */
    u16 *nextSendBufp;               /* +0x1C; 主循环准备的包。 */
    u16 *currentSendBufp;            /* +0x20; IRQ 发送的包。 */
    u16 *currentRecvBufp;            /* +0x24; IRQ 写入的两玩家槽。 */
    u16 *lastRecvBufp;               /* +0x28; 已发布，等待主循环取得。 */
    u16 *recvCheckBufp;              /* +0x2C; 主循环校验/取消息。 */
    struct MultiSioPacket sendBuf[2]; /* +0x30 / +0x50。 */
    struct MultiSioPacket recvBuf[3][MULTI_SIO_PLAYERS_MAX]; /* +0x70 / +0xB0 / +0xF0。 */
};

/**
 * @brief 双向分块传输的活动会话
 * @note 各消息保留独立签名；peerIndex 为主机 1、从机 0。传输标志由 system 的卡片服务启用。
 * @address 0x03004F20，大小 0x60。
 */
struct SerialLinkSession
{
    struct SerialLinkMessage send; /* +0x00; 下一次发出的协议消息。 */
    struct SerialLinkMessage received[MULTI_SIO_PLAYERS_MAX]; /* +0x18 / +0x30。 */
    u32 transportStatus;            /* +0x48; MULTI_SIO_* 返回标志组合。 */
    u8 handshakeStarted;           /* +0x4C; 见到玩家 0 后写本机签名并置位。 */
    u8 peerIndex;                   /* +0x4D; 对端在 received 中的索引。 */
    u8 receivedBlockCount;         /* +0x4E; 只在块号、校验和均有效时递增。 */
    u8 blockCount;                  /* +0x4F; 参数 blockCount 的低八位。 */
    const u8 *sendData;             /* +0x50; 收到请求时按 blockIndex * 16 取数据。 */
    u8 *receiveData;                /* +0x54; 响应数据先复制到这里，再检查字节和。 */
    u16 transferPolls;             /* +0x58; 活动阶段 poll 次数，按半字回绕。 */
    u16 lastProgressPoll;          /* +0x5A; 新块请求/当前块响应更新时间；0 禁用超时检查。 */
    u8 handshakePolls;             /* +0x5C; 无签名累计十次后回到初始化。 */
    u8 lastServedBlock;            /* +0x5D; 抑制重复请求，初始为 0xFF。 */
    u8 transferEnabled;            /* +0x5E; 非零才处理块协议；配置值只保留低八位。 */
    u8 reserved5F;                 /* +0x5F; 仅见初始化清零。 */
};

/**
 * @brief ConfigureTransfer 保存、PollBlockTransfer 消费的传输参数
 * @note 与活动会话分开，初始化会话不清这些参数；长度以 16 字节块计。
 * @address 0x03004F80，COMMON 分配 0x10 字节。
 */
struct SerialLinkTransfer
{
    const u8 *sendData;             /* +0x00 */
    u8 *receiveData;                /* +0x04 */
    u16 blockCount;                 /* +0x08; byteSize >> 4 的低十六位。 */
    u16 phase;                      /* +0x0A; SerialLinkTransferPhase。 */
    u16 autoStart;                  /* +0x0C; 初始化时写入 session.transferEnabled。 */
    u16 reserved0E;                 /* +0x0E; 保留原对象的尾部对齐。 */
};

typedef char SerialLinkBlockSizeCheck[(sizeof(union SerialLinkBlock) == 0x10) ? 1 : -1];
typedef char SerialLinkMessageSizeCheck[(sizeof(struct SerialLinkMessage) == 0x18) ? 1 : -1];
typedef char SerialLinkMessageBlockCheck[(offsetof(struct SerialLinkMessage, block) == 4) ? 1 : -1];
typedef char SerialLinkMessageTagCheck[(offsetof(struct SerialLinkMessage, blockTag) == 0x14) ? 1 : -1];
typedef char SerialLinkPacketSizeCheck[(sizeof(struct MultiSioPacket) == 0x20) ? 1 : -1];
typedef char SerialLinkPacketMessageCheck[(offsetof(struct MultiSioPacket, data) == 4) ? 1 : -1];
typedef char SerialLinkTransportSizeCheck[(sizeof(struct MultiSioArea) == 0x130) ? 1 : -1];
typedef char SerialLinkTransmitIndexCheck[(offsetof(struct MultiSioArea, sendBufCounter) == 0x14) ? 1 : -1];
typedef char SerialLinkReceiveIndexCheck[(offsetof(struct MultiSioArea, recvBufCounter) == 0x18) ? 1 : -1];
typedef char SerialLinkTransmitWriteCheck[(offsetof(struct MultiSioArea, nextSendBufp) == 0x1C) ? 1 : -1];
typedef char SerialLinkReceiveReadCheck[(offsetof(struct MultiSioArea, recvCheckBufp) == 0x2C) ? 1 : -1];
typedef char SerialLinkTransmitStorageCheck[(offsetof(struct MultiSioArea, sendBuf) == 0x30) ? 1 : -1];
typedef char SerialLinkReceiveStorageCheck[(offsetof(struct MultiSioArea, recvBuf) == 0x70) ? 1 : -1];
typedef char SerialLinkSessionSizeCheck[(sizeof(struct SerialLinkSession) == 0x60) ? 1 : -1];
typedef char SerialLinkSessionReceiveCheck[(offsetof(struct SerialLinkSession, received) == 0x18) ? 1 : -1];
typedef char SerialLinkSessionStatusCheck[(offsetof(struct SerialLinkSession, transportStatus) == 0x48) ? 1 : -1];
typedef char SerialLinkSessionSendDataCheck[(offsetof(struct SerialLinkSession, sendData) == 0x50) ? 1 : -1];
typedef char SerialLinkSessionPollsCheck[(offsetof(struct SerialLinkSession, transferPolls) == 0x58) ? 1 : -1];
typedef char SerialLinkSessionEnabledCheck[(offsetof(struct SerialLinkSession, transferEnabled) == 0x5E) ? 1 : -1];
typedef char SerialLinkTransferSizeCheck[(sizeof(struct SerialLinkTransfer) == 0x10) ? 1 : -1];
typedef char SerialLinkTransferPhaseCheck[(offsetof(struct SerialLinkTransfer, phase) == 0x0A) ? 1 : -1];

/* COMMON definitions belong to serial_link.c, in original address order. */
extern struct MultiSioArea gMultiSioArea; /* @address 0x03004DF0 */
extern struct SerialLinkSession gSerialLinkSession;     /* @address 0x03004F20 */
extern struct SerialLinkTransfer gSerialLinkTransfer;   /* @address 0x03004F80 */

void MultiSioInit(void);
u32 MultiSioMain(void *recvp);
void MultiSioSendDataSet(const void *sendp);
u32 MultiSioRecvDataCheck(void *recvp);
void SerialLink_ServiceVBlank(void);
void MultiSioIntr(void);
void MultiSioStart(void);
void MultiSioStop(void);
s32 SerialLink_ExchangePackets(s32 enableMaster);
s32 SerialLink_PollBlockTransfer(void);
s32 SerialLink_HasPeerSignature(void);
void SerialLink_ResetSession(void);
void SerialLink_ConfigureTransfer(void *receiveData, const void *sendData,
    u32 byteSize, u16 autoStart);
void SerialLink_Close(void);
void SerialLink_CopyWords(void *destination, const void *source, u32 wordCount);

/**
 * @brief 启用块传输并请求对端第零块
 * @note 保留卡片服务两处相同的 byte/halfword 写入顺序；没有独立函数或存储分配。
 * @address 内联于 Link_UpdateCardExchange，原入口 0x0800096C。
 */
static inline void SerialLink_RequestBlockTransferInline(void)
{
    gSerialLinkSession.transferEnabled = 1;
    gSerialLinkSession.send.command = SERIAL_LINK_COMMAND_REQUEST_BLOCK;
}

#endif
