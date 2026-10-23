#include "wb_port.h"
#include <string.h>

size_t wb_check_imports(const so_module *module,
                       const so_default_dynlib *bindings, size_t count,
                       void (*report_missing)(const char *name)) {
    size_t missing = 0;
    /* Only call on a validated, loaded ELF. Inspect symbol metadata before
     * invoking so_resolve: its return value does not report missing imports. */
    for (uint32_t i = 1; i < module->num_dynsym; ++i) {
        const Elf32_Sym *symbol = &module->dynsym[i];
        if (symbol->st_shndx != SHN_UNDEF || !symbol->st_name) continue;
        const char *name = module->dynstr + symbol->st_name;
        size_t j = 0;
        for (; j < count; ++j)
            if (bindings[j].func && !strcmp(bindings[j].symbol, name)) break;
        if (j == count) {
            ++missing;
            if (report_missing) report_missing(name);
        }
    }
    return missing;
}
