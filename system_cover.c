#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

#include <qemu-plugin.h>


#define PLUGIN_VERSION      "0.1.0"

const char description[] __attribute__ ((section ("plugin_desc"))) =
"Qemu TCG plugin for collecting coverage information. Ver. "PLUGIN_VERSION
// this is gcc extension to write multi-line strings (available with gnu99)
R"(
Arguments:
    [filename] - path to the output file (default to qemu-%pid%.cover)
)";


QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define MAGIC_WORD      0xbabadedadeadbeafull
#define START_SIZE      (1024*1024)
#define DELTA_SIZE      (64*1024)

struct data_t {
    uint64_t addr;
    uint64_t size;
    uint64_t cnt;
};

static FILE *logfile;
static struct data_t *data;
static uint64_t ind, max;

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    fwrite(data, sizeof(struct data_t), ind, logfile);
    fflush(logfile);
    fclose(logfile);
    free(data);
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    if (ind == max) {
        return;
    }

    data[ind].addr = qemu_plugin_tb_vaddr(tb);
    data[ind].size = qemu_plugin_tb_n_insns(tb);
    data[ind].cnt = 0;

    qemu_plugin_register_vcpu_tb_exec_inline(tb, QEMU_PLUGIN_INLINE_ADD_U64,
                                             &data[ind++].cnt, 1);

    if (ind == max) {
        struct data_t *tmp;
        tmp = realloc(data, (max + DELTA_SIZE)*sizeof(struct data_t));
        if (tmp) {
            max += DELTA_SIZE;
            data = tmp;
        }
    }
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info,
                                           int argc, char **argv)
{
    if (argc > 1) {
        logfile = fopen(argv[1], "wb");
    } else {
        char str[64];
        snprintf(str, 64, "qemu-%u.cover", getpid());
        logfile = fopen(str, "wb");
    }

    if (!logfile) {
        return -1;
    }

    ind = 0;
    max = START_SIZE;
    data = realloc(data, START_SIZE*sizeof(struct data_t));
    if (!data) {
        return -2;
    }

    uint64_t magic = MAGIC_WORD;
    fwrite(&magic, sizeof(uint64_t), 1, logfile);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
