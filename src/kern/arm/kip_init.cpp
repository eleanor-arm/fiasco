INTERFACE [arm]:

#include "kip.h"

class Kip_init
{
public:
  static void init();
};


//---------------------------------------------------------------------------
IMPLEMENTATION [arm]:

#include <cstring>

#include "config.h"
#include "mem_layout.h"
#include "mem_space.h"
#include "mem_unit.h"
#include "timer.h"


// Make the stuff below appearing only in this compilation unit.
// Trick Preprocess to let the struct reside in the cc file rather
// than putting it into the _i.h file which is perfectly wrong in 
// this case.
namespace KIP_namespace
{
  // See also restrictions about KIP layout in the kernel linker script!
  enum
  {
    Num_mem_descs = 64,
    Max_len_version = 512,

    Size_mem_descs = sizeof(Mword) * 2 * Num_mem_descs,
  };

  struct KIP
  {
    Kip kip;
    char mem_descs[Size_mem_descs];
  };

  KIP my_kernel_info_page asm("my_kernel_info_page") __attribute__((section(".kernel_info_page"))) =
    {
      {
	/* 00/00  */ L4_KERNEL_INFO_MAGIC,
	             Config::Kernel_version_id,
	             (Size_mem_descs + sizeof(Kip)) >> 4,
	             {}, 0, 0, {},
	/* 10/20  */ 0, {},
	/* 20/40  */ 0, 0, {},
	/* 30/60  */ 0, 0, {},
	/* 40/80  */ 0, 0, {},
	/* 50/A0  */ 0, (sizeof(Kip) << (sizeof(Mword)*4)) | Num_mem_descs, {},
	/* 60/C0  */ {},
	/* A0/140 */ 0, 0, 0, 0,
	/* B8/160 */ 0, {},
	/* E0/1C0 */ 0, 0, {},
	/* F0/1D0 */ {"", 0, {0}},
      },
      {}
    };
};

IMPLEMENT
void Kip_init::init()
{
  // Don't reference KIP::my_kernel_info_page directly because the actual
  // object contains more data: The linker script adds version information and
  // also extends the size to 4KiB. Using KIP::my_kernel_info_page directly
  // worries the compiler.
  extern char my_kernel_info_page[];
  Kip *kinfo = reinterpret_cast<Kip*>(my_kernel_info_page);
  Kip::init_global_kip(kinfo);
  kinfo->add_mem_region(Mem_desc(0, Mem_space::user_max(),
                        Mem_desc::Conventional, true));
  init_syscalls(kinfo);
}

PUBLIC static
void
Kip_init::init_kip_clock()
{
  union K
  {
    Kip       k;
    Unsigned8 b[Config::PAGE_SIZE];
  };
  extern char kip_time_fn_read_us[];
  extern char kip_time_fn_read_us_end[];
  extern char kip_time_fn_read_ns[];
  extern char kip_time_fn_read_ns_end[];

  K *k = reinterpret_cast<K *>(Kip::k());

  *reinterpret_cast<Mword*>(k->b + 0x970) = Timer::get_scaler_ts_to_us();
  *reinterpret_cast<Mword*>(k->b + 0x978) = Timer::get_shift_ts_to_us();
  *reinterpret_cast<Mword*>(k->b + 0x9f0) = Timer::get_scaler_ts_to_ns();
  *reinterpret_cast<Mword*>(k->b + 0x9f8) = Timer::get_shift_ts_to_ns();

  memcpy(k->b + 0x900, kip_time_fn_read_us,
         kip_time_fn_read_us_end - kip_time_fn_read_us);
  memcpy(k->b + 0x980, kip_time_fn_read_ns,
         kip_time_fn_read_ns_end - kip_time_fn_read_ns);

  Mem_unit::make_coherent_to_pou(k->b + 0x900, 0x100);
}

//--------------------------------------------------------------
IMPLEMENTATION[64bit]:

PRIVATE static inline
void
Kip_init::init_syscalls(Kip *kinfo)
{
  union K
  {
    Kip k;
    Mword w[0x1000 / sizeof(Mword)];
  };
  K *k = reinterpret_cast<K *>(kinfo);
  k->w[0x800 / sizeof(Mword)] = 0xd65f03c0d4000001; // svc #0; ret
}

//--------------------------------------------------------------
IMPLEMENTATION[32bit]:

PRIVATE static inline
void
Kip_init::init_syscalls(Kip *)
{}

//--------------------------------------------------------------
IMPLEMENTATION[mpu]:

#include "kmem.h"

/**
 * Map KIP in separate region to make it accessible to user space.
 *
 * There is a dedicated KIP region in the MPU so that user space can get
 * access to it. So far the kernel region covered it. Remove it from there
 * and establish the dedicated region.
 */
PUBLIC static
void
Kip_init::map_kip(Kip *k)
{
  extern char _kernel_image_start;
  extern char _kernel_kip_end;

  auto diff = Kmem::kdir->del(reinterpret_cast<Mword>(&_kernel_image_start),
                              reinterpret_cast<Mword>(&_kernel_kip_end) - 1U);
  diff |= Kmem::kdir->add(reinterpret_cast<Mword>(k),
                          reinterpret_cast<Mword>(k) + 0xfffU,
                          Mpu_region_attr::make_attr(L4_fpage::Rights::RWX()),
                          false, Kpdir::Kip);

  if (!diff)
    panic("Cannot map KIP\n");

  Mpu::sync(*Kmem::kdir, diff.value(), true);
}

//--------------------------------------------------------------
IMPLEMENTATION[!mpu]:

PUBLIC static inline
void
Kip_init::map_kip(Kip *)
{}
