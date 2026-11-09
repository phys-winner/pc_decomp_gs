/* QA applies immediately and must survive cancelling the ordinary settings page. */
#include "../app/native/host/grief_config.c"
#include <assert.h>

static int lan;
int void_grief_lan_active(void) { return lan; }
const char *void_data_dir(void) { return "/data/local/tmp/grief-qa-unused"; }
int void_vm_readable(uint32_t address, uint32_t size) { (void)address; (void)size; return 0; }
void void_dsound_master_volume(int volume) { (void)volume; }
void void_log(const char *format, ...) { (void)format; }

int main(void)
{
    loaded = 1;
    void_grief_qa_configure(7);
    assert(void_grief_qa_flags() == 7);
    void_grief_config_command(CFG_SET_UNLOCK_STAGES, 0);
    assert(void_grief_qa_flags() == 6);
    void_grief_config_command(CFG_CANCEL, 0);
    assert(void_grief_qa_flags() == 7);
    void_grief_qa_configure(0);
    void_grief_config_command(CFG_SET_UNLOCK_STAGES, 1);
    void_grief_config_command(CFG_CANCEL, 0);
    assert(void_grief_qa_flags() == 0);
    void_grief_qa_configure(2);
    void_grief_config_command(CFG_CANCEL, 0);
    assert(void_grief_qa_flags() == 2);
    void_grief_qa_configure(0);
    lan = 1;
    void_grief_qa_configure(7);
    assert(void_grief_qa_flags() == 0);
    puts("PASS: QA survives settings Cancel; disabled QA stays disabled; LAN rejects cheats");
    return 0;
}
