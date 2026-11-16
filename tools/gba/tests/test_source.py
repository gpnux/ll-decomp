"""Check storage ownership without confusing references and local variables."""

import unittest

from tools.gba.source import storage_definitions


class StorageDefinitionTests(unittest.TestCase):
    def test_tentative_arrays_pointers_and_initializers(self):
        source = '''
unsigned count;
static unsigned phase, ticks[4];
const unsigned values[2] = {1, 2};
struct State *current;
static void (*callback)(void);
unsigned aligned[8] __attribute__((aligned(4)));
'''
        self.assertEqual(storage_definitions(source), dict(count=False, phase=True, ticks=True,
                         values=False, current=False, callback=True, aligned=False))

    def test_extern_types_function_parameters_and_locals_are_not_storage(self):
        source = '''
extern unsigned external;
typedef unsigned Word;
typedef void (*Callback)(void);
struct State { unsigned member; };
void Register(void (*argument)(void));
unsigned Read(void) { static unsigned local; return local; }
unsigned global;
'''
        self.assertEqual(storage_definitions(source), {"global": False})

    def test_comments_literals_and_initializer_references_are_not_definitions(self):
        source = '''
// unsigned comment;
char *message = "unsigned string; { }";
extern unsigned existing;
unsigned *reference = &existing;
unsigned after;
'''
        self.assertEqual(storage_definitions(source), dict(message=False, reference=False, after=False))

    def test_conditional_and_macro_definitions_remain_unresolved(self):
        source = '''
#define DEFINE_STORAGE() \\
    unsigned macro;
#if 1
unsigned conditional;
#else
unsigned fallback;
#endif
unsigned actual;
'''
        self.assertEqual(storage_definitions(source), dict(actual=False))


if __name__ == "__main__":
    unittest.main()
