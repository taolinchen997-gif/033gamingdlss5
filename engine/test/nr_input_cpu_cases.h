#pragma once
#include "../src/nr_input_policy.h"
#include "../src/nr_call_scope.h"
#include <thread>
static void nrInputChecks(){
 using namespace nrinput033;
 check(!CanPresent(false,false,true,false,0,false,true,Route::None),"NR off cannot acquire presentation route");
 check(!CanPresent(true,true,true,false,0,false,true,Route::None),"safe mode cannot activate presentation NR");
 check(!CanPresent(true,false,false,false,0,false,true,Route::None),"legacy engine cannot create integrated fallback");
 check(CanPresent(true,false,true,false,0,false,true,Route::None),"RE4 enabled with no upscale callbacks can send real image");
 check(!CanPresent(true,false,true,true,0,false,true,Route::None),"native preferred route is not stolen at startup");
 check(!CanPresent(true,false,true,false,1,false,true,Route::None),"offered upscale cannot race into presentation route");
 check(!CanPresent(true,false,true,false,0,true,true,Route::None),"native FG rejects presentation processing");
 check(!CanPresent(true,false,true,false,0,false,false,Route::None),"game-owned command list cannot skip state gate");
 check(!CanPresent(true,false,true,false,0,false,true,Route::Upscale),"claimed upscale path rejects second NR pass");
 check(CanPresent(true,false,true,true,7,false,true,Route::Presentation),"late upscale offer cannot change an established NR input route");
 Ownership present;check(present.Claim(Route::Presentation),"presentation route can claim before building model");
 check(!present.Claim(Route::Upscale),"late native callback cannot create competing model");
 check(present.Claim(Route::Presentation),"F11 off-on can reuse the same route and model");
 Ownership upscale;check(upscale.Claim(Route::Upscale),"upscale first retains its route");
 check(!upscale.Claim(Route::Presentation),"presentation may not take over an existing upscale model");
 for(int i=0;i<100;++i){Ownership race;bool a=false,b=false;
  std::thread first([&]{a=race.Claim(Route::Upscale);}),second([&]{b=race.Claim(Route::Presentation);});first.join();second.join();
  check(a!=b,"concurrent input callbacks choose exactly one model route");
 }
 check(HasNativeGuide(true)&&NeedsGameState()&&Reset(0)==0,"native guide/state/reset contract remains intact");
 {PresentScope scope;check(!HasNativeGuide(true)&&!HasNativeGuide(false),"zero guide placeholders never reported as native data");
  check(!NeedsGameState()&&Reset(0)==1,"runtime immediate list uses explicit state and resets missing temporal guides");
  {PresentScope nested;check(context.presentation,"nested presentation scope retained");}
  check(context.presentation,"outer presentation scope retained");}
 check(!context.presentation&&NeedsGameState()&&Reset(0)==0,"native state checks restored after presentation callback");
 {nrdispatch::AfterScope own;check(own.entered,"presentation takes common writer");
  nrdispatch::WriterAccess stage;check(stage.entered,"existing host NR stage reuses callback writer");
  nrdispatch::AfterScope other;check(!other.entered,"reentrant native offer cannot race NR working textures");}
 {nrdispatch::AfterScope next;check(next.entered,"writer is released after callback");}
}
