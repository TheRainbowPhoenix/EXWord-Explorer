/* Experimental UPDADN2 full-application bootstrap.
 * Loaded by Test Menu at P1 0x8c190000, maps an embedded gint app
 * (linked at 0x70000080) to its expected virtual address.
 *
 * NO guarantee the engineering test menu exposes the add-in RAM map.
 * Never patch MMU entries unless 8 contiguous 4-KiB 0x7400 RAM pages
 * and sufficient free user TLB entries are already present.
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define UTLB_ADDRESS ((volatile u32 *)0xf6000000u)
#define UTLB_DATA ((volatile u32 *)0xf7000000u)
#define MMUCR (*(volatile u32 *)0xff000010u)
#define APP_BASE 0x70000000u
#define RAM_BASE 0x74000000u
/* File: .header(128), bootstrap page(4096), gint payload at offset 0x1100.
 * Therefore virtual 0x70000000 maps to physical 0x0c191000.
 * The gint entry function is at virtual 0x70000080. */
#define APP_PHYS 0x0c191000u
#define APP_ENTRY 0x70000080u
#define APP_MAX_PAGES 32
#define SCREEN_W 528
#define SCREEN_H 320
#define PRDR (*(volatile u8 *)0xa405013cu)
#define LCD (*(volatile u16 *)0xb4000000u)

static void lcd_command(u8 value)
{
    PRDR=(u8)(PRDR&~0x10);
    __asm__ volatile("synco":::"memory");
    LCD=value;
    __asm__ volatile("synco":::"memory");
    PRDR=(u8)(PRDR|0x10);
    __asm__ volatile("synco":::"memory");
}
static void lcd_fill(u16 value)
{
    lcd_command(0x2a);LCD=0;LCD=0;LCD=2;LCD=15;
    lcd_command(0x2b);LCD=0;LCD=40;LCD=1;LCD=103;
    lcd_command(0x2c);
    for(unsigned i=0;i<SCREEN_W*SCREEN_H;i++) {
        LCD=value;
    }
    lcd_command(0x29);
}
/* Failure shows a fixed color, with no heap, libc, interrupts or OS calls:
 * red = unsupported OS/MMU environment, blue = application returned. */
static void fail(unsigned code)
{
    lcd_fill(code==1 ? 0xf800u : 0x001fu);
    for(;;) __asm__ volatile("nop");
}
static u32 tlb_page(int slot)
{
    return UTLB_ADDRESS[slot*64] & 0xfffff000u;
}
static int ram_mapping_available(void)
{
    /* The normal gint runtime expects 8 contiguous 4-KiB pages of OS RAM
       at 0x74000000, where the EX-word syscalls dispatcher is stored. */
    u32 physical=0;
    for(int page=0;page<8;page++) {
        int found=0;
        for(int i=0;i<64;i++) {
            u32 a=UTLB_ADDRESS[i*64];
            u32 d=UTLB_DATA[i*64];
            if((a&0x300u)!=0x300u || (d&0x100u)==0 ||
                tlb_page(i)!=(RAM_BASE+(u32)page*4096)) continue;
            /* The same 0x17c flags are used by gint's 4-KiB page pinning.
               The 4-KiB page-size bits are 0x10/0x80 (SZ1/SZ2). */
            if((d&0x90u)!=0x10u) continue;
            u32 p=d&0x1ffff000u;
            if(page==0)physical=p;
            else if(p!=physical+(u32)page*4096) return 0;
            found=1;break;
        }
        if(!found)return 0;
    }
    return 1;
}
/* Hand off control to the full app ONLY when all required free TLB
   slots are available. This avoids destroying OS mappings in Test Menu. */
void updater_full_boot(void)
{
    u32 control=MMUCR;
    if((control&0x81u)!=1u) fail(1);
    if(!ram_mapping_available()) fail(1);
    int available=(int)((control>>18)&63u);
    if(!available) available=64;
    if(available<=8)fail(1);

    /* The packer records the full app payload's size at the start of the
       4-KiB stub page's final 16 bytes; it is read at a P1 fixed address.
       Data is stored in big-endian SH convention. */
    volatile u32 const *metadata=(volatile u32 const *)0x8c190ff0u;
    if(metadata[0]!=0x45584653u /* EXFS */ || !metadata[1] ||
       metadata[1]>128u*1024u)fail(1);
    u32 pages=(metadata[1]+128u+4095u)/4096u;
    if(pages<1||pages>APP_MAX_PAGES)fail(1);

    /* Choose only free entries BEFORE the last eight slots reserved by
       gint's exword_mmu. Never clobber any valid mapping. */
    unsigned slots[APP_MAX_PAGES];
    unsigned used=0;
    for(int i=0;i<available-8 && used<pages;i++) {
        u32 a=UTLB_ADDRESS[i*64],d=UTLB_DATA[i*64];
        if(!(a&0x100u) && !(d&0x100u))slots[used++]=(unsigned)i;
    }
    if(used!=pages)fail(1);

    /* Call into the gint _exword_entry compiled at 0x70000080. Its
       mmu_exword_prepare() validates the exact same RAM mapping again. */
    for(u32 i=0;i<pages;i++) {
        unsigned slot=slots[i]*64u;
        UTLB_ADDRESS[slot]=0;
        UTLB_DATA[slot]=(APP_PHYS+i*4096u)|0x17cu;
        UTLB_ADDRESS[slot]=(APP_BASE+i*4096u)|0x300u;
    }
    __asm__ volatile("synco":::"memory");

    /* EX-word's full app uses its own gint interrupt/driver handlers.
       The bootstrap does not call or claim those facilities itself. */
    int (*app)(void)=(int (*)(void))APP_ENTRY;
    (void)app();

    /* We are back in the P1 bootstrap; remove only our new mappings. */
    for(u32 i=0;i<pages;i++) {
        unsigned slot=slots[i]*64u;
        UTLB_ADDRESS[slot]=0;
        UTLB_DATA[slot]=0;
    }
    __asm__ volatile("synco":::"memory");
    fail(2);
}
