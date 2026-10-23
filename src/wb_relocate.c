#include "wb_port.h"
#include <stdio.h>
#include <string.h>
static int mapped_word(const so_module *m,uintptr_t p) {
    if(p%4) return 0;
    if(p>=m->text_base && p-m->text_base<=m->text_size-4) return 1;
    for(int i=0;i<m->n_data;++i)
        if(m->data_size[i]>=4 && p>=m->data_base[i] &&
           p-m->data_base[i]<=m->data_size[i]-4) return 1;
    return 0;
}
/* Complete relocation and resolution in one pass, after a validation pass.
 * In particular ABS32 is S+A, not S. Never install unresolved-call stubs.
 * Only the hash-pinned ARM ELF is accepted by the launcher. */
int wb_relocate(so_module *m,const so_default_dynlib *table,size_t count) {
    if(wb_check_imports(m,table,count,NULL)) return -1;
    for(unsigned pass=0;pass<2;++pass)
        for(uint32_t i=0;i<m->num_reldyn+m->num_relplt;++i) {
            const Elf32_Rel *r=i<m->num_reldyn ? &m->reldyn[i] : &m->relplt[i-m->num_reldyn];
            unsigned type=ELF32_R_TYPE(r->r_info),index=ELF32_R_SYM(r->r_info);
            if(type==R_ARM_NONE) continue;
            uintptr_t address=m->load_addr+r->r_offset;
            if(index>=m->num_dynsym || address<m->load_addr || !mapped_word(m,address))
                return -1;
            if(type!=R_ARM_RELATIVE && type!=R_ARM_ABS32 &&
               type!=R_ARM_GLOB_DAT && type!=R_ARM_JUMP_SLOT) return -1;
            if(type==R_ARM_RELATIVE && index) return -1;
            const Elf32_Sym *s=&m->dynsym[index];
            uintptr_t value=0;
            if(type==R_ARM_RELATIVE) value=m->load_addr;
            else if(s->st_shndx!=SHN_UNDEF)
                value=(s->st_shndx==SHN_ABS ? 0 : m->load_addr)+s->st_value;
            else {
                const char *name=m->dynstr+s->st_name;
                size_t j=0;
                for(;j<count;++j) if(!strcmp(table[j].symbol,name)) { value=table[j].func; break; }
                if(!value) { fprintf(stderr,"Unresolved import %s\n",name); return -1; }
            }
            if(pass) {
                uint32_t *word=(uint32_t *)address;
                *word=(uint32_t)value+((type==R_ARM_ABS32 || type==R_ARM_RELATIVE) ? *word : 0);
            }
        }
    return 0;
}
