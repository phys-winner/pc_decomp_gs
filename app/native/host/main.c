#define _GNU_SOURCE
#include "host.h"
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

int void_modules_start(void);
void void_files_std_init(void);
static int exiting;

void void_process_exit(uint32_t code, int run_detach)
{
    if (!exiting && run_detach) {
        exiting = 1;
        void_modules_detach_all(1);
    }
    void_log("[grief] process exit %u", code);
    void_log_flush();
#ifdef VOID_ANDROID_APP
    void void_android_process_exit(uint32_t code);
    void_android_process_exit(code);
#endif
    _exit((int)code);
}

int void_game_main(const char *game, const char *data, const char *images, int selftest)
{
    void_install_fault_handlers();
    mkdir(data, 0775);
    char log_path[1024];
    snprintf(log_path, sizeof log_path, "%s/griefsyndrome.log", data);
    void_log_open(log_path);
    void_log("[grief] static host: game=%s data=%s images=%s", game, data, images);
    if (!void_guest_reserve()) return 65;
    void_threads_init();
    void_dispatch_init();
    if (!void_files_init(game, data)) return 66;
    void void_grief_config_init(void);
    void_grief_config_init();
    void_files_std_init();
    if (!void_modules_init(images)) return 67;
    void_machine_enter();
    if (!void_modules_start()) return 68;
    int void_grief_lan_start(void);
    if (!void_grief_lan_start()) return 69;
    uint32_t base = void_module_exe_base();
    uint32_t entry = base + MEM32(base + MEM32(base + 0x3C) + 0x28);
    if (selftest) {
        int code = void_dispatch_find(entry) ? 0 : 71;
        void_log("[selftest] PE, imports and dispatch initialized; entry %08X, result %d", entry, code);
        void_machine_leave();
        void_log_flush();
        return code;
    }
    void_log("[grief] starting lifted entry %08X", entry);
    int code = (int)void_call_guest(entry, 0, NULL, 1);
    void_log("[grief] entry returned %d", code);
    void_machine_leave();
    void_log_flush();
    return code;
}

#ifndef VOID_ANDROID_APP
int main(int argc, char **argv)
{
    const char *game = "/data/local/tmp/griefsyndrome/game";
    const char *data = "/data/local/tmp/griefsyndrome/userdata";
    const char *images = "/data/local/tmp/griefsyndrome/images";
    int selftest = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--data") && i + 1 < argc) data = argv[++i];
        else if (!strcmp(argv[i], "--images") && i + 1 < argc) images = argv[++i];
        else if (!strcmp(argv[i], "--edition") && i + 1 < argc) {
            const char *edition = argv[++i];
            if (strcmp(edition, "jp") && strcmp(edition, "ru")) return 64;
            void_module_select_image("griefsyndrome.exe");
            void_ansi_set_codepage(!strcmp(edition, "ru") ? 1251u : 932u);
        } else if (!strcmp(argv[i], "--selftest")) selftest = 1;
        else if (!strcmp(argv[i], "-v")) void_verbose = 1;
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 64; }
    }
    setvbuf(stderr, NULL, _IONBF, 0);
    int code = void_game_main(game, data, images, selftest);
    void_process_exit((uint32_t)code, 0);
    return code;
}
#endif
