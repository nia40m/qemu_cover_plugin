#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#include <qemu-plugin.h>

#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

#define PLUGIN_VERSION      "0.1.0"

const char description[] __attribute__ ((section ("plugin_desc"))) =
"Qemu TCG plugin for collecting coverage information. Ver. "PLUGIN_VERSION
// this is gcc extension to write multi-line strings (available with gnu99)
R"(
Arguments:
    [filename] - path to the output file (default to qemu-%%pid%%.cover)
)";


QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAGIC_WORD      0xbabadedadeadbeafull
#define DATA_COUNT      (256*1024)
#define ARRAY_SIZE      100

struct data_t {
    uint64_t addr;
    uint64_t size;
    uint64_t cnt;
};

struct wrapper_t {
    struct data_t *data;
    uint32_t filled;
};

static FILE *logfile;
static struct wrapper_t *data_array;
static uint32_t data_curr, data_last;

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    uint32_t i;

    for (i = 0; i <= data_curr; i++) {
        if (data_array[i].filled == 0) {
            break;
        }

        fwrite(data_array[i].data, sizeof(struct data_t), data_array[i].filled, logfile);
        fflush(logfile);
        free(data_array[i].data);
    }

    fclose(logfile);
    free(data_array);
}

static struct data_t *get_data_elem(void)
{
    /* check for wrapper array overflow */
    if (unlikely(data_curr == data_last)) {
        void *temp = realloc(data_array,
            (data_last + 1 + ARRAY_SIZE)*sizeof(struct wrapper_t));

        if (likely(temp)) {
            data_array = temp;
            data_last += ARRAY_SIZE;
        } else {
            /* FIXME: how to handle this? */
            exit(-1);
        }
    }

    /* check for data array overflow */
    if (unlikely(data_array[data_curr].filled == DATA_COUNT)) {
        void *temp = malloc(DATA_COUNT*sizeof(struct data_t));

        if (likely(temp)) {
            data_array[data_curr + 1].filled = 0;
            data_array[data_curr + 1].data = temp;
            data_curr++;
        } else {
            /* FIXME: how to handle this? */
            exit(-1);
        }
    }

    return &data_array[data_curr].data[data_array[data_curr].filled++];
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    struct data_t *data = get_data_elem();

    data->addr = qemu_plugin_tb_vaddr(tb);
    data->size = 0;
    data->cnt = 0;

    /* calculate basic block size in bytes */
    for (size_t i = 0; i < qemu_plugin_tb_n_insns(tb); i++) {
        data->size += qemu_plugin_insn_size(qemu_plugin_tb_get_insn(tb, i));
    }

    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &data->cnt, 1);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    char str[256];

    if (argc > 0) {
        snprintf(str, sizeof(str), "qemu-%u-%s.cover", getpid(), argv[0]);
    } else {
        snprintf(str, sizeof(str), "qemu-%u.cover", getpid());
    }

    logfile = fopen(str, "wb");
    if (!logfile) {
        return -1;
    }

    data_array = malloc(ARRAY_SIZE*sizeof(struct wrapper_t));
    data_curr = 0;
    data_last = ARRAY_SIZE - 1;
    if (!data_array) {
        return -2;
    }

    data_array[data_curr].data = malloc(DATA_COUNT*sizeof(struct data_t));
    data_array[data_curr].filled = 0;
    if (!data_array[data_curr].data) {
        free(data_array);
        return -3;
    }

    uint64_t magic = MAGIC_WORD;
    fwrite(&magic, sizeof(uint64_t), 1, logfile);
    fflush(logfile);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
