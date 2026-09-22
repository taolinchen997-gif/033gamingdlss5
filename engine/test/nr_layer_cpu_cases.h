#pragma once
#include "../src/nr_layer_bank.h"
#include "../src/nr_feature_params.h"
#include <string>
namespace layercpu {
struct Config {
    float intensity=1.23f,local_structure=1.45f,local_tone=.67f,skin_structure=-1.f,global_tone=.81f;
    int style=2,preset=3,auto_mask=0,ui_correct=0,passes=3;
    nrlayers::Model extra[2]={nrlayers::Neutral(1),nrlayers::Neutral(2)};
};
struct Bank {
    void* feat=nullptr;void* extra_feat[2]{};
    void *full=nullptr,*model_input=nullptr,*out=nullptr,*res=nullptr,*spare_full=nullptr,*spare_res=nullptr,
        *extra_out=nullptr,*extra_alt=nullptr,*pass_input=nullptr,*pass_input3=nullptr,*hold_depth=nullptr,*hold_motion=nullptr,*hold_white=nullptr;
    void* hist[2]{};void* refined[2]{};
    Config model_cfg;int selected_passes=3,built_passes=3;
};
template<class Check> void Run(Check check){
    using namespace nrlayers;unsigned checks=0;
    auto test=[&](bool b,const char* why){++checks;check(b,why);};
    test(!InitializationComplete(0,1)&&!InitializationComplete(9,10)&&!InitializationComplete(UINT64_MAX,10),"unsubmitted/incomplete/device-lost initialization cannot publish");
    test(InitializationComplete(10,10)&&InitializationComplete(11,10)&&!InitializationComplete(0,0),"publication requires a real completed fence target");
    test(BorrowStillValid(true,17,17)&&!BorrowStillValid(true,17,18)&&BorrowStillValid(false,17,18),"resource retirement during build invalidates a borrowing candidate");
    Config legacy;Reader old;old.Finish(legacy);
    for(int pass=1;pass<3;++pass){const auto m=Get(legacy,pass);const auto previous=nrstack::ForPass(pass,legacy.intensity,legacy.local_structure,legacy.local_tone,legacy.skin_structure,legacy.global_tone,legacy.style,legacy.preset);
        test(m.intensity==previous.intensity&&m.structure==previous.structure&&m.tone==previous.tone&&m.skin==previous.skin&&m.globalTone==previous.globalTone&&m.style==previous.style&&m.preset==previous.preset,"legacy migration preserves each old refinement request exactly");
        test(m.autoMask==0&&m.uiCorrect==0,"legacy masks migrated after parsing first-layer keys");
    }
    Config migrated=legacy;migrated.style=0;migrated.auto_mask=1;migrated.ui_correct=1;
    test(Equal(Get(migrated,1),Get(legacy,1))&&Equal(Get(migrated,2),Get(legacy,2)),"editing first layer never re-migrates later layers");
    Reader partial;partial.Read("nrlayer2.style","2");partial.Read("nrlayer3.automask","1");partial.Finish(migrated);
    test(migrated.extra[0].style==2&&migrated.extra[1].style==0&&migrated.extra[1].autoMask==1,"partial new config uses independent per-key defaults");
    for(const char* bad:{"nan","inf","-2","4","2garbage",""}){
        Reader r;r.Read("nrlayer2.style",bad);Config c;r.Finish(c);test(c.extra[0].style==0,"invalid layer setting cannot poison model request");
    }
    // Real file roundtrip of persisted inactive layers (including exact 1/3).
    for(int count=1;count<=3;++count){
        Config original=legacy;original.passes=count;original.extra[0].style=1;original.extra[1].preset=2;
        original.extra[0].skin=-1;original.extra[1].globalTone=1.79f;
        FILE* f=nullptr;fopen_s(&f,"nr-layer-roundtrip.cfg","w+b");test(f!=nullptr,"open local CPU fixture");
        if(f){Save(f,original);std::rewind(f);Reader r;char line[256];
            while(std::fgets(line,sizeof(line),f)){if(auto eq=std::strchr(line,'=')){*eq=0;r.Read(line,eq+1);}}
            std::fclose(f);Config restored=legacy;r.Finish(restored);
            for(int pass=1;pass<3;++pass)test(Equal(Get(original,pass),Get(restored,pass)),"save/restart retains all inactive layer floats and styles exactly");
        }
    }
    int objects[32]{};Bank live;live.feat=&objects[0];live.extra_feat[0]=&objects[1];live.extra_feat[1]=&objects[2];live.full=&objects[3];live.hist[0]=&objects[4];live.hold_depth=&objects[5];live.model_cfg=legacy;
    FeatureParams<int> params;
    test(!params.Any()&&!params.Find(nullptr),"empty core registry never aliases a null feature");
    for(int i=0;i<6;++i){auto* slot=params.Free();test(slot!=nullptr,"three active and three candidate core parameter blocks fit");if(slot)*slot={&objects[i],&objects[10+i]};}
    test(!params.Free()&&params.Any(),"core registry fails closed at its bounded capacity");
    for(int i=0;i<6;++i)test(params.Find(&objects[i])&&params.Find(&objects[i])->params==&objects[10+i],"evaluation and release resolve each handle's own core parameter block");
    auto* released=params.Find(&objects[1]);test(released&&released->params==&objects[11],"failed release can preserve exact parameter block");
    *released={};test(!params.Find(&objects[1])&&params.Find(&objects[0])&&params.Find(&objects[2]),"successful single release never clears another layer's record");
    *params.Free()={&objects[6],&objects[16]};test(params.Find(&objects[6])->params==&objects[16]&&!params.Find(&objects[1]),"released slot reused without stale handle alias");
    for(int a=0;a<3;++a)for(int b=0;b<3;++b)for(int c=0;c<3;++c){
        Config requested=legacy;requested.style=a;requested.extra[0].style=b;requested.extra[1].style=c;
        Bank candidate=live;int calls[3]{};
        bool ok=ReplaceFeatures(candidate,live,requested,[&](int pass,Model tune,void*& out){
            ++calls[pass];test(Equal(tune,Get(requested,pass)),"create callback receives this layer's complete request");out=&objects[6+pass];return true;});
        test(ok,"three independently selected supported styles prepare successfully");
        for(int pass=0;pass<3;++pass)test(calls[pass]==(!Equal(Get(legacy,pass),Get(requested,pass))?1:0),"only changed layers create features");
        test(live.feat==&objects[0]&&live.extra_feat[0]==&objects[1]&&live.extra_feat[1]==&objects[2],"candidate creation never changes active handles");
        auto discarded=candidate;ExcludeBorrowed(discarded,live);
        test(!discarded.full&&!discarded.hist[0]&&!discarded.hold_depth,"discard never retires borrowed frame/history/hold resources");
        auto retired=live;ExcludeBorrowed(retired,candidate);
        for(int pass=0;pass<3;++pass){auto handle=pass?retired.extra_feat[pass-1]:retired.feat;test((handle!=nullptr)==(calls[pass]!=0),"successful publish retires only replaced handles");}
    }
    for(int pass=0;pass<3;++pass)for(int field=0;field<9;++field){
        Config requested=legacy;Model m=Get(requested,pass);
        switch(field){case 0:m.intensity+=.0001f;break;case 1:m.structure+=.01f;break;case 2:m.tone+=.01f;break;case 3:m.skin+=.01f;break;case 4:m.globalTone+=.01f;break;case 5:m.style=(m.style+1)%3;break;case 6:m.preset=(m.preset+1)%4;break;case 7:m.autoMask=1-m.autoMask;break;case 8:m.uiCorrect=1-m.uiCorrect;break;}
        if(pass)requested.extra[pass-1]=m;else{requested.intensity=m.intensity;requested.local_structure=m.structure;requested.local_tone=m.tone;requested.skin_structure=m.skin;requested.global_tone=m.globalTone;requested.style=m.style;requested.preset=m.preset;requested.auto_mask=m.autoMask;requested.ui_correct=m.uiCorrect;}
        test(Signature(requested)!=Signature(legacy),"every model parameter affects rebuild signature, even sub-percent edits");
        Bank candidate=live;int calls=0;
        test(ReplaceFeatures(candidate,live,requested,[&](int layer,Model tune,void*& out){++calls;test(layer==pass&&Equal(tune,m),"single parameter update belongs only to selected layer");out=&objects[8];return true;})&&calls==1,"one edited parameter builds exactly one layer");
    }
    for(int failed=0;failed<3;++failed){Config requested=legacy;requested.style=1;requested.extra[0].style=1;requested.extra[1].style=2;Bank candidate=live;
        test(!ReplaceFeatures(candidate,live,requested,[&](int pass,Model,void*& out){out=pass==failed?nullptr:&objects[10+pass];return pass!=failed;}),"creation failure rejects partial candidate");
        ExcludeBorrowed(candidate,live);test(!candidate.full&&!candidate.hist[0],"failure cleanup excludes all borrowed GPU resources");
        test(live.feat==&objects[0]&&live.extra_feat[0]==&objects[1]&&Equal(Get(live.model_cfg,1),Get(legacy,1)),"failure preserves active model handles and applied parameters");
    }
    Config disabled=legacy;disabled.passes=1;auto savedSig=Signature(disabled,true),runSig=Signature(disabled);disabled.extra[1].style=2;
    test(Signature(disabled)==runSig&&Signature(disabled,true)!=savedSig,"disabled layer changes save without rebuilding active models");
    Bank candidate=live;int calls=0;test(ReplaceFeatures(candidate,live,disabled,[&](int,Model,void*&){++calls;return true;})&&calls==0,"reducing layer count reuses cached features");
    test(candidate.model_cfg.extra[1].style==legacy.extra[1].style,"disabled cached layer keeps its actual create snapshot");
    disabled.passes=3;test(!Matches(candidate.model_cfg,disabled,3),"re-enabling edited inactive layer cannot falsely hit model cache");
    candidate=live;candidate.extra_feat[1]=nullptr;candidate.built_passes=2;
    auto two=candidate;calls=0;test(ReplaceFeatures(candidate,two,legacy,[&](int pass,Model,void*& out){test(pass==2,"adding layer keeps prior handles");++calls;out=&objects[14];return true;})&&calls==1,"growing stack builds only missing layer");
    // Active bank can be retired while initialization is outstanding. The
    // saved borrow set prevents double-retirement even after live becomes empty.
    auto cancelled=candidate;Bank empty;ExcludeBorrowed(cancelled,empty);ExcludeBorrowed(cancelled,two);
    test(!cancelled.full&&!cancelled.feat&&!cancelled.extra_feat[0]&&cancelled.extra_feat[1]==&objects[14],"cancel after owner release retires only candidate-owned objects");
    std::printf("NR LAYER CPU: %u checks; migration, exact persistence, create requests, reuse, rollback; no GPU/runtime adoption test\n",checks);
}
}
