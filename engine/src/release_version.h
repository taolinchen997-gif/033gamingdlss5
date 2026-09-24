#pragma once
// One product version shared by the panel, runtime log and PE resource.
// Vendor runtime versions retain their own identity in the dependency lock.
#if defined(RC_INVOKED)
#define K033_PRODUCT_VERSION "033 YanYun S39 Test"
#else
#define K033_PRODUCT_VERSION "燕云定制版"
#endif
#define K033_VERSION_NUMBERS 6,1,3,0
#define K033_VERSION_FILE "6.1.3.0"
#define K033_ENGINE_VERSION "033 YanYun S39 / international client wwm.exe (Steam) runs the core; NR key: most keys and mouse buttons, refused keys say why; S38 RTX 20/30 FG conditions, S36 033 tuning / 20260924"
