/* Cache the pure resource-name hash, never a resource pointer. Names are
 * compared against owned bytes on every hit: mutable/reused string storage
 * cannot return stale results. Contending threads take the original path. */
#include "wb_name_hash.h"
#include <stdatomic.h>
#include <string.h>
enum { SLOTS=1024, TEXT=128 };
static struct {
    const char *address;
    uint64_t value;
    int length;
    unsigned char bytes[TEXT];
} entries[SLOTS];
static atomic_flag guard=ATOMIC_FLAG_INIT;
static uint64_t (*original)(const char *,int);
void wb_name_hash_bind(uint64_t (*fn)(const char *,int)) { original=fn; }
uint64_t wb_name_hash(const char *name,int length) {
    if(!name || !length || length>=TEXT ||
       atomic_flag_test_and_set_explicit(&guard,memory_order_acquire))
        return original(name,length);
    uintptr_t key=(uintptr_t)name;
    unsigned slot=(uint32_t)((key^(key>>7))*2654435761u)>>(32-10);
    int normalized=length<0 ? -1 : length;
    /* A positive count hashes embedded NULs, except an initial NUL, which
     * the original defines as zero. Never read past that initial NUL. */
    unsigned bytes=length>0 ? (*name ? (unsigned)length : 1) : 0;
    if(entries[slot].address==name && entries[slot].length==normalized &&
       (length<0 ? !strcmp(name,(const char *)entries[slot].bytes) :
                    !memcmp(name,entries[slot].bytes,bytes))) {
        uint64_t result=entries[slot].value;
        atomic_flag_clear_explicit(&guard,memory_order_release);return result;
    }
    if(length<0) {
        while(bytes<TEXT && name[bytes]) ++bytes;
        if(bytes==TEXT) {
            atomic_flag_clear_explicit(&guard,memory_order_release);
            return original(name,length);
        }
        ++bytes;
    }
    uint64_t result=original(name,length);
    memcpy(entries[slot].bytes,name,bytes);
    entries[slot].address=name;entries[slot].length=normalized;entries[slot].value=result;
    atomic_flag_clear_explicit(&guard,memory_order_release);return result;
}
