// Shared by the embedded HLSL and CPU numerical checks. The includer either
// stringifies the body for D3DCompile or compiles it with CPU vector helpers.
K033_PORTRAIT_MATH(
float PortraitRegionWeight(float2 uv,float4 box,float4 eye,float4 mouth){
 float2 center=box.xy+box.zw*.5f;
 float2 p=(uv-center)/max(box.zw,float2(.001f,.001f));
 float oval=1-smoothstep(.30f,.48f,length(p*float2(1,1.03f)));
 float2 ep=(uv-eye.xy)/max(box.zw*.16f,.001f);
 float2 eq=(uv-eye.zw)/max(box.zw*.16f,.001f);
 float2 mp=(uv-mouth.xy)/max(box.zw*float2(.23f,.14f),.001f);
 float feature=smoothstep(.55f,1.3f,length(ep))*smoothstep(.55f,1.3f,length(eq))*smoothstep(.5f,1.2f,length(mp));
 // Detection and landmarks define the face region. A fixed RGB skin-colour
 // gate would silently veto a recognized face under cool/desaturated lighting.
 return oval*feature*mouth.z;
}
float SkinColourWeight(float r,float g,float b){
 // A smooth chromatic cue across the CURRENT frame, independent of a face
 // detector or absolute brightness. This cannot distinguish similarly coloured
 // materials from skin; it is not semantic person segmentation.
 float total=max(r+g+b,.0001f);r/=total;g/=total;b/=total;
 float red=smoothstep(.33f,.36f,r)*(1-smoothstep(.49f,.57f,r));
 float green=smoothstep(.24f,.28f,g)*(1-smoothstep(.37f,.41f,g));
 float order=smoothstep(0.f,.025f,r-g)*smoothstep(0.f,.025f,g-b);
 return saturate(red*green*order);
}
float PortraitFeatureProtection(float2 uv,float4 box,float4 eye,float4 mouth){
 float2 center=box.xy+box.zw*.5f;
 float2 p=(uv-center)/max(box.zw,float2(.001f,.001f));
 float oval=1-smoothstep(.30f,.48f,length(p*float2(1,1.03f)));
 float2 ep=(uv-eye.xy)/max(box.zw*.16f,.001f);
 float2 eq=(uv-eye.zw)/max(box.zw*.16f,.001f);
 float2 mp=(uv-mouth.xy)/max(box.zw*float2(.23f,.14f),.001f);
 float feature=smoothstep(.55f,1.3f,length(ep))*smoothstep(.55f,1.3f,length(eq))*smoothstep(.5f,1.2f,length(mp));
 return 1-oval*(1-feature)*mouth.z;
}
float SkinCombinedWeight(float colour,float face,float protection,float match){
 return saturate(max(colour,face*match)*(1-(1-protection)*match));
}
float PortraitMotionWeight(float mask,float mismatch){
 return saturate(mask*(1-smoothstep(.045f,.13f,mismatch)));
}
float PortraitLumaGain(float y,float detail,float weight){
 // Lift the low-frequency lighting under the skin texture, before NR.
 // A bounded exposure-like curve has visible midtone response and rolls off
 // towards highlights. Identical channel gain preserves hue and saturation.
 float base=clamp(y-detail,0.f,1.f);
 float black=smoothstep(.003f,.04f,base);
 float lift=.48f*black*saturate(1-base);
 return 1.f+saturate(weight)*lift;
}
)
