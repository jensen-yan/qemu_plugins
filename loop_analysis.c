#include <stdio.h>
#include <inttypes.h>
#include <glib.h>
#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static GHashTable *loop_starts;         // 记录循环开始地址
static GHashTable *loop_ends;           // 记录循环结束地址
static GHashTable *loop_counts;         // 记录循环执行次数
static GHashTable *instruction_counts;  // 记录每条指令的执行次数, vaddr -> count

static void vcpu_instruction(unsigned int cpu_index, void *udata)
{
    uint64_t vaddr = (uint64_t)udata;
    uint64_t *count = g_hash_table_lookup(instruction_counts, GUINT_TO_POINTER(vaddr));
    if (!count) {
        count = g_malloc(sizeof(*count));
        *count = 0;
        g_hash_table_insert(instruction_counts, GUINT_TO_POINTER(vaddr), count);
    }
    (*count)++;     // 这个地址的指令执行次数加1

    if (g_hash_table_contains(loop_starts, GUINT_TO_POINTER(vaddr))) {  // 如果是循环开始
        uint64_t *loop_count = g_hash_table_lookup(loop_counts, GUINT_TO_POINTER(vaddr));
        if (!loop_count) {
            loop_count = g_malloc(sizeof(*loop_count));
            *loop_count = 0;
            g_hash_table_insert(loop_counts, GUINT_TO_POINTER(vaddr), loop_count);
        }
        (*loop_count)++;
    }
}


static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    size_t i;
    uint64_t tb_vaddr = qemu_plugin_tb_vaddr(tb);

    for (i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
        const char *disas = qemu_plugin_insn_disas(insn);

        printf("0x%" PRIx64 ": %s\n", vaddr, disas);

        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_instruction,
                                               QEMU_PLUGIN_CB_NO_REGS,
                                               GUINT_TO_POINTER(vaddr));

        // 检测循环开始和结束
        if (strncmp(disas, "beq", 3) == 0 || 
            strncmp(disas, "bne", 3) == 0 || 
            strncmp(disas, "blt", 3) == 0 || 
            strncmp(disas, "bge", 3) == 0 || 
            strncmp(disas, "bltu", 4) == 0 || 
            strncmp(disas, "bgeu", 4) == 0) {
            
            // 提取跳转目标地址
            char *target_str = strrchr(disas, '#');
            if (target_str) {
                target_str += 2; // 跳过 "# "
                int64_t offset;
                uint64_t target_addr;

                if (strchr(target_str, '-')) {
                    // 处理负偏移
                    offset = -strtoll(target_str + 1, NULL, 16);
                    target_addr = vaddr + offset;
                } else {
                    // 处理正偏移或绝对地址
                    target_addr = strtoull(target_str, NULL, 16);
                }
                
                // 向后跳转可能表示循环结束
                if (target_addr < vaddr) {
                    g_hash_table_insert(loop_ends, GUINT_TO_POINTER(vaddr), GUINT_TO_POINTER(1));
                    g_hash_table_insert(loop_starts, GUINT_TO_POINTER(target_addr), GUINT_TO_POINTER(1));
                    
                    printf("Possible loop detected: Start at 0x%" PRIx64 ", End at 0x%" PRIx64 "\n", 
                           target_addr, vaddr);
                }
            }
        }
    }
}



static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    GHashTableIter iter;
    gpointer key, value;

    g_hash_table_iter_init(&iter, loop_counts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        uint64_t addr = GPOINTER_TO_UINT(key);
        uint64_t count = *(uint64_t *)value;
        printf("Loop at 0x%" PRIx64 " executed %" PRIu64 " times\n", addr, count);
    }

    // 输出每条指令的执行次数，可以用来分析循环体内的指令
    g_hash_table_iter_init(&iter, instruction_counts);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        uint64_t addr = GPOINTER_TO_UINT(key);
        uint64_t count = *(uint64_t *)value;
        printf("Instruction at 0x%" PRIx64 " executed %" PRIu64 " times\n", addr, count);
    }

    g_hash_table_destroy(loop_starts);
    g_hash_table_destroy(loop_ends);
    g_hash_table_destroy(loop_counts);
    g_hash_table_destroy(instruction_counts);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    loop_starts = g_hash_table_new(g_direct_hash, g_direct_equal);
    loop_ends = g_hash_table_new(g_direct_hash, g_direct_equal);
    loop_counts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    instruction_counts = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}