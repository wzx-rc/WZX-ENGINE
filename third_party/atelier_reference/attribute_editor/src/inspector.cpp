#include "inspector.h"
#include "inspector_icons.h"
#include "imgui.h"
#include "imgui_internal.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace atelier::inspector {
namespace {
float uiScale=1.f;
ImU32 Opacity(ImU32 color){unsigned a=(color>>24)&255u;unsigned scaled=(unsigned)std::round(a*ImGui::GetStyle().Alpha);return (color&0x00ffffffu)|(std::min(255u,scaled)<<24);}
ImVec2 Physical(ImVec2 p){return {p.x*uiScale,p.y*uiScale};}
ImVec2 Logical(ImVec2 p){return {p.x/uiScale,p.y/uiScale};}
void Cursor(ImVec2 p){ImGui::SetCursorScreenPos(Physical(p));}
void NextWidth(float width){ImGui::SetNextItemWidth(width*uiScale);}
struct ScaledDraw {
 ImDrawList* raw=nullptr;
 ScaledDraw& operator=(ImDrawList* value){raw=value;return *this;}
 ScaledDraw& operator=(std::nullptr_t){raw=nullptr;return *this;}
 void AddText(ImFont* font,float size,ImVec2 p,ImU32 c,const char* text,const char* end=nullptr,float wrap=0){raw->AddText(font,size*uiScale,Physical(p),Opacity(c),text,end,wrap*uiScale);}
 void AddRectFilled(ImVec2 a,ImVec2 b,ImU32 c,float r=0){raw->AddRectFilled(Physical(a),Physical(b),Opacity(c),r*uiScale);}
 void AddRect(ImVec2 a,ImVec2 b,ImU32 c,float r=0,float stroke=1){raw->AddRect(Physical(a),Physical(b),Opacity(c),r*uiScale,stroke*uiScale);}
 void AddLine(ImVec2 a,ImVec2 b,ImU32 c,float stroke=1){raw->AddLine(Physical(a),Physical(b),Opacity(c),stroke*uiScale);}
 void AddCircle(ImVec2 p,float r,ImU32 c,int n=0,float stroke=1){raw->AddCircle(Physical(p),r*uiScale,Opacity(c),n,stroke*uiScale);}
 void AddCircleFilled(ImVec2 p,float r,ImU32 c,int n=0){raw->AddCircleFilled(Physical(p),r*uiScale,Opacity(c),n);}
 void AddRectFilledMultiColor(ImVec2 a,ImVec2 b,ImU32 c1,ImU32 c2,ImU32 c3,ImU32 c4){raw->AddRectFilledMultiColor(Physical(a),Physical(b),Opacity(c1),Opacity(c2),Opacity(c3),Opacity(c4));}
 void TriangleGradient(ImVec2 a,ImVec2 b,ImVec2 c,ImU32 ca,ImU32 cb,ImU32 cc){ImVec2 uv=ImGui::GetFontTexUvWhitePixel();raw->PrimReserve(3,3);ImDrawIdx idx=(ImDrawIdx)raw->_VtxCurrentIdx;raw->PrimWriteIdx(idx);raw->PrimWriteIdx((ImDrawIdx)(idx+1));raw->PrimWriteIdx((ImDrawIdx)(idx+2));raw->PrimWriteVtx(Physical(a),uv,Opacity(ca));raw->PrimWriteVtx(Physical(b),uv,Opacity(cb));raw->PrimWriteVtx(Physical(c),uv,Opacity(cc));}
 void PushClipRect(ImVec2 a,ImVec2 b,bool intersect=false){raw->PushClipRect(Physical(a),Physical(b),intersect);}
 void PopClipRect(){raw->PopClipRect();}
};
ScaledDraw scaledDraw;
struct Rect {float x,y,w,h;ImVec2 Min()const{return {x,y};}ImVec2 Max()const{return {x+w,y+h};}};
constexpr ImU32 BG=IM_COL32(23,28,32,255),PANEL=IM_COL32(28,33,38,255),RAISED=IM_COL32(37,43,49,255);
constexpr ImU32 BORDER=IM_COL32(49,57,64,255),TEXT=IM_COL32(201,208,216,255),MUTED=IM_COL32(153,165,177,255);
constexpr ImU32 CYAN=IM_COL32(102,186,208,255),SELECT=IM_COL32(43,72,88,255),COPPER=IM_COL32(208,148,110,255);
constexpr ImU32 HOVER=IM_COL32(70,84,95,255),TRACK=IM_COL32(53,64,74,255);
ScaledDraw* draw=&scaledDraw;
InspectorState* current=nullptr;
void DrawGlyph(ScaledDraw* target,Glyph glyph,ImVec2 position,float size,ImU32 color){atelier::inspector::DrawGlyph(target->raw,glyph,Physical(position),size*uiScale,Opacity(color),uiScale);}

void Text(float x,float y,const std::string& text,ImU32 color=TEXT,float size=12.f){draw->AddText(ImGui::GetFont(),size,{x,y},color,text.c_str());}
float TextWidth(const std::string& text,float size=12.f){return ImGui::GetFont()->CalcTextSizeA(size,10000,0,text.c_str()).x;}
void Center(Rect r,const std::string& text,ImU32 color=TEXT,float size=12.f){Text(r.x+(r.w-TextWidth(text,size))/2,r.y+(r.h-size)/2,text,color,size);}
void Box(Rect r,ImU32 color=BG,float radius=4,ImU32 border=BORDER){draw->AddRectFilled(r.Min(),r.Max(),color,radius);if(border)draw->AddRect(r.Min(),r.Max(),border,radius,1.f);}
void Register(const std::string& id,Rect r,bool enabled=true){current->controls.push_back({id,r.x*uiScale,r.y*uiScale,r.w*uiScale,r.h*uiScale,enabled});}
bool Hit(const std::string& id,Rect r,bool enabled=true){Register(id,r,enabled);Cursor(r.Min());if(!enabled)ImGui::BeginDisabled();bool hit=ImGui::InvisibleButton(id.c_str(),Physical({r.w,r.h}),ImGuiButtonFlags_EnableNav);if(!enabled)ImGui::EndDisabled();return hit;}
void Tip(const char* message){if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled|ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",message);}
bool IconButton(const std::string& id,Rect r,Glyph glyph,bool enabled=true,const char* tooltip=nullptr){
 bool hit=Hit(id,r,enabled);bool hover=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled),focused=ImGui::IsItemFocused();
 if(enabled&&(hover||focused))Box(r,RAISED,3,focused?CYAN:HOVER);
 ImU32 color=enabled?(hover||focused?CYAN:MUTED):IM_COL32(153,165,177,61);
 DrawGlyph(draw,glyph,{r.x+(r.w-12)/2,r.y+(r.h-12)/2},12,color);if(tooltip)Tip(tooltip);return hit;
}
bool TextButton(const std::string& id,Rect r,const char* label,bool primary=false){bool hit=Hit(id,r);bool hovered=ImGui::IsItemHovered();Box(r,primary?SELECT:RAISED,4,hovered||ImGui::IsItemFocused()?CYAN:primary?IM_COL32(76,116,133,255):HOVER);Center(r,label,primary?IM_COL32(219,234,240,255):TEXT);return hit;}
void TrackEdit(const Document& before){if(ImGui::IsItemActivated())current->BeginEdit(before);if(ImGui::IsItemDeactivated())current->EndEdit();}
void ChangedDot(float x,float y,bool changed){if(changed)draw->AddCircleFilled({x,y},2,COPPER,12);}
void Label(float x,float y,const char* label,bool changed=false){Text(x,y,label);ChangedDot(x+TextWidth(label)+8,y+6,changed);}
std::string Ellipsis(const std::string& s,float maximum,float font=12){if(TextWidth(s,font)<=maximum)return s;std::string out=s;while(!out.empty()&&TextWidth(out+"...",font)>maximum){size_t end=out.size();--end;while(end>0&&((unsigned char)out[end]&0xc0)==0x80)--end;out.resize(end);}return out+"...";}
bool Match(const std::string& name){std::string a=name,b=current->search;std::transform(a.begin(),a.end(),a.begin(),[](unsigned char c){return (char)std::tolower(c);});std::transform(b.begin(),b.end(),b.begin(),[](unsigned char c){return (char)std::tolower(c);});return b.empty()||a.find(b)!=std::string::npos;}
void Numeric(const std::string& id,Rect r,float& value,float minimum,float maximum,int decimals,const char* unit="",const char* prefix="",bool enabled=true,bool document=true){
 Register(id,r,enabled);Document before=current->value;float unitWidth=*unit?TextWidth(unit,11)+8:0,prefixWidth=*prefix?TextWidth(prefix,11)+9:0;
 bool editing=ImGui::GetCurrentContext()->ActiveId==ImGui::GetID(("##"+id).c_str());Box(r,BG,4);
 if(*prefix)Text(r.x+7,r.y+8,prefix,MUTED,11);if(*unit)Text(r.x+r.w-unitWidth+1,r.y+8,unit,MUTED,11);
 Cursor({r.x+prefixWidth,r.y});NextWidth(std::max(24.f,r.w-prefixWidth-unitWidth));
 ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Physical({7,8}));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
 ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,IM_COL32(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_FrameBgActive,IM_COL32(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_Text,editing?TEXT:IM_COL32(0,0,0,0));
 char format[24];std::snprintf(format,sizeof(format),"%%.%df",decimals);if(!enabled)ImGui::BeginDisabled();
 bool changed=ImGui::InputFloat(("##"+id).c_str(),&value,0,0,format,ImGuiInputTextFlags_CharsDecimal);if(changed){if(!std::isfinite(value))value=minimum;value=std::clamp(value,minimum,maximum);}
 if(document)TrackEdit(before);bool active=ImGui::IsItemActive(),hover=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled),focus=ImGui::IsItemFocused();if(!enabled)ImGui::EndDisabled();
 ImGui::PopStyleColor(4);ImGui::PopStyleVar(2);
 if(active||focus)draw->AddRect(r.Min(),r.Max(),CYAN,4,1.f);else if(hover)draw->AddRect(r.Min(),r.Max(),HOVER,4,1.f);
 if(!active){char text[40];std::snprintf(text,sizeof(text),format,value);ImU32 color=!enabled&&ImGui::GetStyle().Alpha>=.999f?IM_COL32(201,208,216,115):TEXT;Text(r.x+r.w-unitWidth-7-TextWidth(text),r.y+8,text,color);}
}
bool ResetButton(const char* id,Rect r,bool changed,bool enabled=true){std::string key=std::string(id)+".reset";if(IconButton(key,r,Glyph::Reset,changed&&enabled,"Restore the initial material value")){current->EndEdit();return current->Reset(id);}return false;}
void Scalar(const char* id,float x,float& y,float w,bool enabled=true){
 const ScalarSpec* spec=nullptr;for(auto& s:ScalarSpecs())if(std::strcmp(s.id,id)==0){spec=&s;break;}if(!spec)return;
 float& value=current->value.*(spec->member);bool modified=value!=(current->defaults.*(spec->member));
 if(!enabled)ImGui::PushStyleVar(ImGuiStyleVar_Alpha,.45f);
 Label(x,y+8,spec->label,modified);Numeric(std::string(id)+".value",{x+w-106,y,76,28},value,spec->minimum,spec->maximum,spec->decimals,spec->unit,"",enabled);
 ResetButton(id,{x+w-22,y,22,28},modified,enabled);
 Rect slider={x,y+31,w-30,16};Register(std::string(id)+".slider",slider,enabled);Cursor(slider.Min());NextWidth(slider.w);
 Document before=current->value;ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Physical({0,2}));ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,7*uiScale);
 const ImGuiCol colors[]={ImGuiCol_FrameBg,ImGuiCol_FrameBgHovered,ImGuiCol_FrameBgActive,ImGuiCol_SliderGrab,ImGuiCol_SliderGrabActive,ImGuiCol_Text};for(auto col:colors)ImGui::PushStyleColor(col,IM_COL32(0,0,0,0));
 if(!enabled)ImGui::BeginDisabled();char format[24];std::snprintf(format,sizeof(format),"%%.%df",spec->decimals);
 ImGui::SliderFloat(("##"+std::string(id)+".slider").c_str(),&value,spec->minimum,spec->maximum,format,ImGuiSliderFlags_AlwaysClamp);TrackEdit(before);
 bool active=ImGui::IsItemActive(),focus=ImGui::IsItemFocused();if(!enabled)ImGui::EndDisabled();ImGui::PopStyleColor(6);ImGui::PopStyleVar(3);
 float t=std::clamp((value-spec->minimum)/(spec->maximum-spec->minimum),0.f,1.f),cx=slider.x+3.5f+t*(slider.w-7.f),cy=slider.y+8;
 draw->AddRectFilled({slider.x,cy-1.5f},{slider.x+slider.w,cy+1.5f},TRACK,2);
 if(enabled)draw->AddRectFilled({slider.x,cy-1.5f},{cx,cy+1.5f},CYAN,2);
 draw->AddRectFilled({cx-3.5f,cy-5.5f},{cx+3.5f,cy+5.5f},enabled?IM_COL32(214,222,229,255):IM_COL32(110,119,127,255),2);
 if(enabled&&(active||focus))draw->AddRect(slider.Min(),slider.Max(),IM_COL32(102,186,208,100),3,1.f);
 if(!enabled){Tip(std::strcmp(id,"opacity")==0?"Opacity is used by Cutout and Transparent surfaces.":"Assign and enable a texture first.");ImGui::PopStyleVar();}y+=48;
}
void Checker(Rect r){draw->PushClipRect(r.Min(),r.Max(),true);for(float yy=r.y;yy<r.y+r.h;yy+=4)for(float xx=r.x;xx<r.x+r.w;xx+=4){int k=(int)((xx-r.x)/4)+(int)((yy-r.y)/4);draw->AddRectFilled({xx,yy},{xx+4,yy+4},k%2?IM_COL32(67,76,85,255):IM_COL32(120,129,138,255));}draw->PopClipRect();}
void Swatch(Rect r,const std::array<float,4>& color,bool active=false){Checker(r);draw->AddRectFilled(r.Min(),r.Max(),ImGui::ColorConvertFloat4ToU32({color[0],color[1],color[2],color[3]}),4);draw->AddRect(r.Min(),r.Max(),active?CYAN:IM_COL32(90,98,106,255),4,1.f);}
void HexField(const std::string& id,Rect r,std::array<float,4>& color,bool document=true){
 Register(id,r);std::string hex=Hex(color);char buffer[16];std::snprintf(buffer,sizeof(buffer),"%s",hex.c_str());Document before=current->value;
 Cursor(r.Min());NextWidth(r.w);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Physical({8,8}));
 if(ImGui::InputText(("##"+id).c_str(),buffer,sizeof(buffer),ImGuiInputTextFlags_CharsUppercase)){std::array<float,4> parsed=color;if(ParseHex(buffer,parsed)){parsed[3]=color[3];color=parsed;}}
 if(document)TrackEdit(before);ImGui::PopStyleVar();
}
void ColorProperty(const char* id,const char* title,float x,float& y,float w,bool alpha){
 auto& c=alpha?current->value.baseColor:current->value.emissionColor;auto& initial=alpha?current->defaults.baseColor:current->defaults.emissionColor;
 Label(x,y,title,c!=initial);y+=23;Rect swatch={x,y,34,28};bool click=Hit(std::string(id)+".swatch",swatch);Swatch(swatch,c,current->pickerOpen&&current->pickerTarget==(alpha?0:1));if(click){current->EndEdit();current->OpenPicker(alpha?0:1);}Tip("Edit color: RGB, HSV, alpha and hexadecimal values");
 float hexWidth=alpha?w-160:w-72;HexField(std::string(id)+".hex",{x+42,y,hexWidth,28},c);
 if(alpha){Document before=current->value;float a=c[3]*100;Numeric("baseColor.alpha",{x+w-110,y,80,28},a,0,100,0,"%","A",true,false);c[3]=a/100;TrackEdit(before);}
 ResetButton(id,{x+w-22,y,22,28},c!=initial);y+=40;
}
void VectorProperty(const char* id,const char* title,float x,float& y,float w,bool scale){
 auto& value=scale?current->value.uvScale:current->value.uvOffset;auto& initial=scale?current->defaults.uvScale:current->defaults.uvOffset;
 Label(x,y,title,value!=initial);y+=24;float field=(w-38)/2;
 for(int i=0;i<2;++i){Rect r={x+i*(field+8),y,field,28};Numeric(std::string(id)+(i==0?".u":".v"),r,value[i],scale?.01f:-20.f,scale?40.f:20.f,2,scale?"×":"",i==0?"U":"V");draw->AddLine({r.x+1,r.y+2},{r.x+1,r.y+26},i==0?IM_COL32(103,126,140,255):IM_COL32(121,145,129,255),2);}
 ResetButton(id,{x+w-22,y,22,28},value!=initial);y+=39;
}
void Toggle(const char* id,const char* title,float x,float& y,float w,bool Document::*member){Rect row={x,y,w,29};bool clicked=Hit(id,row);Text(x,y+8,title);Rect cb={x+w-14,y+7,14,14};bool value=current->value.*member;Box(cb,value?SELECT:BG,3,value?CYAN:HOVER);if(value)DrawGlyph(draw,Glyph::Check,cb.Min(),14,CYAN);if(clicked){current->EndEdit();Document before=current->value;current->value.*member=!value;current->Commit(before);}y+=29;}
void Combo(const char* id,Rect r,int& value,const char* const* names,int count){
 Register(id,r);Document before=current->value;Cursor(r.Min());NextWidth(r.w);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Physical({8,8}));
 if(ImGui::BeginCombo((std::string("##")+id).c_str(),names[std::clamp(value,0,count-1)],ImGuiComboFlags_NoArrowButton)){for(int i=0;i<count;++i)if(ImGui::Selectable(names[i],value==i)){current->EndEdit();value=i;current->Commit(before);}ImGui::EndCombo();}
 ImGui::PopStyleVar();DrawGlyph(draw,Glyph::ChevronDown,{r.x+r.w-19,r.y+7},14,MUTED);
}
bool Section(const char* key,const char* title,const char* keywords,int index,float x,float& y,float w,const char* count=""){
 if(!Match(std::string(title)+" "+keywords))return false;y+=8;Rect r={x,y,w,28};bool clicked=Hit(std::string("section.")+key,r);Box(r,RAISED,3,0);bool open=current->sections[index]||current->search[0];DrawGlyph(draw,open?Glyph::ChevronDown:Glyph::ChevronRight,{x+8,y+8},12,MUTED);Text(x+28,y+8,title);if(*count)Text(x+w-8-TextWidth(count,11),y+8,count,MUTED,11);if(clicked)current->sections[index]=!current->sections[index];y+=28;if(open)y+=10;return open;
}
void InlineCombo(const char* id,const char* label,float x,float& y,float w,int& value,const char* const* names,int count){y+=8;Text(x,y+8,label,MUTED);Combo(id,{x+w-178,y,178,28},value,names,count);y+=36;}
void Properties(float x,float& y,float w){
 if(Section("surface","Surface","Base Color Roughness Metallic Normal strength Ambient occlusion",0,x,y,w,"PBR")){ColorProperty("baseColor","Base Color",x,y,w,true);for(auto id:{"roughness","metallic","normalStrength","ao"})Scalar(id,x,y,w);y+=2;}
 if(Section("detail","Surface Detail","Pattern Pattern scale",1,x,y,w)){const char* names[]={"Smooth","Brushed Metal","Ancient Stone","Wood Grain","Checker","Woven Fabric"};InlineCombo("pattern","Pattern",x,y,w,current->value.pattern,names,6);Scalar("patternScale",x,y,w);y+=2;}
 if(Section("coat","Coat & Anisotropy","Clearcoat Coat roughness Anisotropy Sheen",2,x,y,w)){for(auto id:{"clearcoat","coatRoughness","anisotropy","sheen"})Scalar(id,x,y,w);y+=2;}
 if(Section("emission","Emission","Emission Color Emission strength",3,x,y,w)){ColorProperty("emissionColor","Emission Color",x,y,w,false);Scalar("emissionStrength",x,y,w);y+=2;}
 if(Section("uv","UV Mapping","Scale Offset Rotation Repeat textures",4,x,y,w)){VectorProperty("uvScale","Scale",x,y,w,true);VectorProperty("uvOffset","Offset",x,y,w,false);Scalar("uvRotation",x,y,w);Toggle("tiling","Repeat textures",x,y,w,&Document::tiling);y+=2;}
 if(Section("transparency","Transparency","Opacity Index of refraction",5,x,y,w)){Scalar("opacity",x,y,w,current->value.shader==1||current->value.shader==2);Scalar("ior",x,y,w);y+=2;}
 if(Section("rendering","Rendering","Two sided Cast shadows Receive shadows",6,x,y,w)){Toggle("twoSided","Two sided",x,y,w,&Document::twoSided);Toggle("castShadows","Cast shadows",x,y,w,&Document::castShadows);Toggle("receiveShadows","Receive shadows",x,y,w,&Document::receiveShadows);y+=2;}
}
void Stage(float x,float& y,float w){if(Section("studio","Lighting","Environment Rotation Exposure Intensity Key Fill Rim",7,x,y,w)){const char* env[]={"Studio","Sunset","Warehouse"};InlineCombo("environment","Environment",x,y,w,current->value.environment,env,3);for(auto id:{"environmentRotation","exposure","intensity","keyIntensity","fillIntensity","rimIntensity"})Scalar(id,x,y,w);y+=2;}if(Section("camera","Camera","Orbit Elevation Distance Ground grid Contact shadows Turntable",8,x,y,w)){for(auto id:{"yaw","pitch","distance"})Scalar(id,x,y,w);Toggle("grid","Ground grid",x,y,w,&Document::grid);Toggle("shadows","Contact shadows",x,y,w,&Document::shadows);Toggle("turntable","Turntable",x,y,w,&Document::turntable);y+=2;}}
std::string ChooseTextureFile(){wchar_t path[32768]{};OPENFILENAMEW dialog{};dialog.lStructSize=sizeof(dialog);dialog.hwndOwner=GetActiveWindow();dialog.lpstrFilter=L"Image files\0*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.tif;*.tiff;*.exr;*.hdr;*.dds\0All files\0*.*\0";dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrTitle=L"Choose texture";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;if(!GetOpenFileNameW(&dialog))return {};int n=WideCharToMultiByte(CP_UTF8,0,path,-1,nullptr,0,nullptr,nullptr);std::string out((size_t)n,'\0');WideCharToMultiByte(CP_UTF8,0,path,-1,out.data(),n,nullptr,nullptr);if(!out.empty())out.pop_back();return out;}
void Texture(float x,float& y,float w){if(!Section("map","Base Color map","Texture Enable Strength Channel File",9,x,y,w))return;
 Rect preview={x,y,w,144};Box(preview,BG);DrawGlyph(draw,Glyph::Image,{x+w/2-12,y+41},24,MUTED);Center({x,y+79,w,24},current->value.texturePath.empty()?"No texture assigned":"Texture assigned",MUTED,11);y+=156;
 Checker({x,y,40,40});DrawGlyph(draw,Glyph::Image,{x+11,y+11},18,TEXT);std::string filename=current->value.texturePath.empty()?"No texture assigned":std::filesystem::u8path(current->value.texturePath).filename().u8string();Text(x+50,y+4,Ellipsis(filename,w-110),TEXT,12);const char* channelLabels[]={"RGB","Red","Green","Blue","Alpha"};Text(x+50,y+24,std::string("Base Color · ")+channelLabels[std::clamp(current->value.textureChannel,0,4)],MUTED,11);
 if(IconButton("texture.choose",{x+w-48,y+5,22,28},Glyph::FolderOpen,true,"Choose a local texture file")){current->EndEdit();std::string file=ChooseTextureFile();if(!file.empty()){Document before=current->value;current->value.texturePath=file;current->value.textureEnabled=true;current->Commit(before);}}
 if(IconButton("texture.clear",{x+w-22,y+5,22,28},Glyph::Clear,!current->value.texturePath.empty(),"Clear texture reference")){current->EndEdit();Document before=current->value;current->value.texturePath.clear();current->value.textureEnabled=false;current->Commit(before);}y+=48;
 Toggle("textureEnabled","Enable texture",x,y,w,&Document::textureEnabled);Scalar("textureStrength",x,y,w,current->value.textureEnabled&&!current->value.texturePath.empty());const char* channels[]={"RGB","Red","Green","Blue","Alpha"};InlineCombo("textureChannel","Channel",x,y,w,current->value.textureChannel,channels,5);
 std::string path=current->value.texturePath.empty()?"Default material value":current->value.texturePath;ImVec2 wrap=ImGui::GetFont()->CalcTextSizeA(11,10000,w-16,path.c_str());Rect file={x,y,w,std::max(34.f,wrap.y+16)};Box(file,BG);draw->AddText(ImGui::GetFont(),11,{x+8,y+8},MUTED,path.c_str(),nullptr,w-16);y+=file.h+11;
}
void MaterialIllustration(float x,float y,const std::array<float,4>& paint){
 // Small 2D radial color illustration, matching the approved CSS thumbnail.
 // This is draw-list geometry, with no material renderer or preview asset.
 const ImVec4 stops[]={{1,217.f/255,155.f/255,1},{209.f/255,125.f/255,70.f/255,1},{paint[0],paint[1],paint[2],1},{66.f/255,27.f/255,16.f/255,1}};
 const float positions[]={0,.15f,.4f,.89f};
 auto tint=[&](ImVec2 p){float t=std::hypot(p.x-(x+14.12f),p.y-(y+12.72f))/29.12f;int n=0;while(n<2&&t>positions[n+1])++n;float a=std::clamp((t-positions[n])/(positions[n+1]-positions[n]),0.f,1.f);const auto& from=stops[n];const auto& to=stops[n+1];return ImGui::ColorConvertFloat4ToU32({from.x+(to.x-from.x)*a,from.y+(to.y-from.y)*a,from.z+(to.z-from.z)*a,1});};
 draw->AddCircleFilled({x+21,y+22},15,IM_COL32(0,0,0,70),48);draw->AddCircleFilled({x+20,y+20},14,IM_COL32(66,27,16,255),48);
 constexpr float tau=6.28318530718f;auto point=[&](int ring,int segment){float a=segment*tau/48;return ImVec2(x+20+std::cos(a)*(13.7f*ring/8),y+20+std::sin(a)*(13.7f*ring/8));};
 for(int ring=0;ring<8;++ring)for(int k=0;k<48;++k){ImVec2 a=point(ring,k),b=point(ring+1,k),c=point(ring+1,k+1),d=point(ring,k+1);draw->TriangleGradient(a,b,c,tint(a),tint(b),tint(c));if(ring)draw->TriangleGradient(a,c,d,tint(a),tint(c),tint(d));}
}
void Identity(float x,float y,float w){Box({x,y,40,40},BG);MaterialIllustration(x,y,current->value.baseColor);
 Rect name={x+50,y,w-82,22};Register("name",name);Document before=current->value;char buffer[256];std::snprintf(buffer,sizeof(buffer),"%s",current->value.name.c_str());Cursor(name.Min());NextWidth(name.w);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Physical({0,3}));ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(0,0,0,0));ImGui::PushStyleColor(ImGuiCol_Border,IM_COL32(0,0,0,0));if(ImGui::InputText("##name",buffer,sizeof(buffer)))current->value.name=buffer;TrackEdit(before);ImGui::PopStyleColor(2);ImGui::PopStyleVar();const char* names[]={"Opaque","Cutout","Transparent","Unlit"};Text(x+50,y+27,std::string("Material  ·  ")+names[current->value.shader],MUTED,11);
 if(IconButton("reset.all",{x+w-22,y+6,22,28},Glyph::Reset,!Equal(current->value,current->defaults),"Restore material defaults")){current->EndEdit();current->Reset("all");}
}
void DraftHSVFromRGB(){ImGui::ColorConvertRGBtoHSV(current->pickerDraft[0],current->pickerDraft[1],current->pickerDraft[2],current->pickerHSV[0],current->pickerHSV[1],current->pickerHSV[2]);}
void DraftRGBFromHSV(){ImGui::ColorConvertHSVtoRGB(current->pickerHSV[0],current->pickerHSV[1],current->pickerHSV[2],current->pickerDraft[0],current->pickerDraft[1],current->pickerDraft[2]);}
void Picker(float width,float height,Rect panel){
 if(current->requestPicker){ImGui::OpenPopup("InspectorColorPicker");current->requestPicker=false;}
 if(!current->pickerOpen){if(ImGui::IsPopupOpen("InspectorColorPicker"))ImGui::ClosePopupToLevel(ImGui::GetCurrentContext()->BeginPopupStack.Size,true);return;}
 float pw=std::min(310.f,width-16),ph=std::min(578.f,height-24);float px=panel.x+panel.w+10;if(px+pw>width-8)px=panel.x-pw-10;if(px<8)px=(width-pw)/2;float py=std::clamp(panel.y+208,8.f,std::max(8.f,height-ph-8));
 ImGui::SetNextWindowPos(Physical({px,py}),ImGuiCond_Always);ImGui::SetNextWindowSize(Physical({pw,ph}),ImGuiCond_Always);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,6*uiScale);ImGui::PushStyleColor(ImGuiCol_PopupBg,RAISED);
 bool visible=ImGui::BeginPopup("InspectorColorPicker",ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
 ImGui::PopStyleColor();ImGui::PopStyleVar(2);
 if(!visible){current->CancelPicker();return;}scaledDraw=ImGui::GetWindowDrawList();draw=&scaledDraw;ImVec2 pos=Logical(ImGui::GetWindowPos());Rect window={pos.x,pos.y,pw,ph};Box({window.x,window.y,pw,36},IM_COL32(32,39,46,255),0,0);Text(window.x+12,window.y+12,current->pickerTarget==0?"Base Color":"Emission Color");
 if(IconButton("picker.close",{window.x+pw-34,window.y+4,22,28},Glyph::Clear,true,"Cancel color editing")){current->CancelPicker();ImGui::CloseCurrentPopup();}
 Cursor({window.x,window.y+36});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,Physical({12,12}));ImGui::BeginChild("PickerContent",Physical({pw,ph-83}),ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);ImGui::PopStyleVar();scaledDraw=ImGui::GetWindowDrawList();draw=&scaledDraw;float x=window.x+12,y=Logical(ImGui::GetCursorScreenPos()).y,w=pw-24;
 Rect rgb={x,y,49,27},hsv={x+49,y,49,27};for(int i=0;i<2;++i){Rect r=i==0?rgb:hsv;bool press=Hit(i==0?"picker.mode.rgb":"picker.mode.hsv",r);Box(r,current->pickerMode==i?SELECT:BG,3);Center(r,i==0?"RGB":"HSV",current->pickerMode==i?TEXT:MUTED,11);if(press)current->pickerMode=i;}Text(x+w-110,y+8,current->pickerMode==0?"RGB · 0–255":"HSV · ° / %",MUTED,11);y+=37;
 Rect sv={x,y,w-26,177},hue={x+w-16,y,16,177};float rr,gg,bb;ImGui::ColorConvertHSVtoRGB(current->pickerHSV[0],1,1,rr,gg,bb);ImU32 hc=ImGui::ColorConvertFloat4ToU32({rr,gg,bb,1});draw->AddRectFilledMultiColor(sv.Min(),sv.Max(),IM_COL32(255,255,255,255),hc,hc,IM_COL32(255,255,255,255));draw->AddRectFilledMultiColor(sv.Min(),sv.Max(),IM_COL32(0,0,0,0),IM_COL32(0,0,0,0),IM_COL32(0,0,0,255),IM_COL32(0,0,0,255));
 Hit("picker.sv",sv);if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)){current->pickerHSV[1]=std::clamp((Logical(ImGui::GetIO().MousePos).x-sv.x)/sv.w,0.f,1.f);current->pickerHSV[2]=1-std::clamp((Logical(ImGui::GetIO().MousePos).y-sv.y)/sv.h,0.f,1.f);DraftRGBFromHSV();}if(ImGui::IsItemFocused()){bool edit=false;if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)){current->pickerHSV[1]-=.01f;edit=true;}if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)){current->pickerHSV[1]+=.01f;edit=true;}if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)){current->pickerHSV[2]+=.01f;edit=true;}if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)){current->pickerHSV[2]-=.01f;edit=true;}if(edit){current->pickerHSV[1]=std::clamp(current->pickerHSV[1],0.f,1.f);current->pickerHSV[2]=std::clamp(current->pickerHSV[2],0.f,1.f);DraftRGBFromHSV();}}
 ImVec2 point={sv.x+current->pickerHSV[1]*sv.w,sv.y+(1-current->pickerHSV[2])*sv.h};draw->AddCircle(point,5,IM_COL32(0,0,0,150),20,3);draw->AddCircle(point,5,IM_COL32(255,255,255,255),20,1.5f);
 const ImU32 rainbow[]={IM_COL32(255,0,0,255),IM_COL32(255,255,0,255),IM_COL32(0,255,0,255),IM_COL32(0,255,255,255),IM_COL32(0,0,255,255),IM_COL32(255,0,255,255),IM_COL32(255,0,0,255)};for(int i=0;i<6;++i){float yy=y+i*hue.h/6;draw->AddRectFilledMultiColor({hue.x,yy},{hue.x+hue.w,yy+hue.h/6},rainbow[i],rainbow[i],rainbow[i+1],rainbow[i+1]);}
 Hit("picker.hue",hue);if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0)){current->pickerHSV[0]=std::clamp((Logical(ImGui::GetIO().MousePos).y-hue.y)/hue.h,0.f,.999999f);DraftRGBFromHSV();}if(ImGui::IsItemFocused()&&(ImGui::IsKeyPressed(ImGuiKey_UpArrow)||ImGui::IsKeyPressed(ImGuiKey_DownArrow))){current->pickerHSV[0]=std::clamp(current->pickerHSV[0]+(ImGui::IsKeyPressed(ImGuiKey_UpArrow)?-.005f:.005f),0.f,.999999f);DraftRGBFromHSV();}float hy=hue.y+current->pickerHSV[0]*hue.h;draw->AddRect({hue.x-2,hy-2.5f},{hue.x+hue.w+2,hy+2.5f},IM_COL32(238,242,244,255),2,1.5f);y+=190;
 Text(x,y+5,"A",MUTED,11);Rect a={x+20,y+4,w-71,12};Checker(a);auto c=current->pickerDraft;ImU32 ac=ImGui::ColorConvertFloat4ToU32({c[0],c[1],c[2],1}),az=ImGui::ColorConvertFloat4ToU32({c[0],c[1],c[2],0});draw->AddRectFilledMultiColor(a.Min(),a.Max(),az,ac,ac,az);Hit("picker.alpha",{a.x,a.y-4,a.w,20},current->pickerTarget==0);if(current->pickerTarget==0){if(ImGui::IsItemActive()&&ImGui::IsMouseDown(0))current->pickerDraft[3]=std::clamp((Logical(ImGui::GetIO().MousePos).x-a.x)/a.w,0.f,1.f);if(ImGui::IsItemFocused()){if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))current->pickerDraft[3]=std::max(0.f,current->pickerDraft[3]-.01f);if(ImGui::IsKeyPressed(ImGuiKey_RightArrow))current->pickerDraft[3]=std::min(1.f,current->pickerDraft[3]+.01f);}}float ax=a.x+a.w*current->pickerDraft[3];draw->AddRectFilled({ax-2,a.y-2},{ax+2,a.y+14},IM_COL32(238,242,244,255));char alpha[24];std::snprintf(alpha,sizeof(alpha),"%.0f%%",current->pickerDraft[3]*100);Text(x+w-38,y+5,alpha,MUTED,11);y+=32;
 const char* rgbLabels[]={"R","G","B","A %"};const char* hsvLabels[]={"H °","S %","V %","A %"};float cell=(w-18)/4;for(int i=0;i<4;++i){float xx=x+i*(cell+6);Text(xx,y,current->pickerMode==0?rgbLabels[i]:hsvLabels[i],MUTED,11);float value=i==3?current->pickerDraft[3]*100:current->pickerMode==0?current->pickerDraft[i]*255:current->pickerHSV[i]*(i==0?360.f:100.f);float before=value;Numeric("picker.channel."+std::to_string(i),{xx,y+17,cell,28},value,0,i==3?100.f:current->pickerMode==0?255.f:i==0?360.f:100.f,0,"","",i!=3||current->pickerTarget==0,false);if(value!=before){if(i==3)current->pickerDraft[3]=value/100;else if(current->pickerMode==0){current->pickerDraft[i]=value/255;DraftHSVFromRGB();}else {current->pickerHSV[i]=value/(i==0?360.f:100.f);DraftRGBFromHSV();}}}y+=55;
 Text(x,y+8,"Hex",MUTED,11);auto draftBefore=current->pickerDraft;HexField("picker.hex",{x+41,y,w-41,28},current->pickerDraft,false);if(current->pickerDraft!=draftBefore)DraftHSVFromRGB();y+=42;
 draw->AddLine({x,y},{x+w,y},BORDER);y+=12;float chip=(w-10)/2;Text(x,y,"Original",MUTED,11);Text(x+chip+10,y,"Current",MUTED,11);y+=20;Rect original={x,y,chip,24},changed={x+chip+10,y,chip,24};if(Hit("picker.original",original)){current->pickerDraft=current->pickerOriginal;DraftHSVFromRGB();}Swatch(original,current->pickerOriginal);Swatch(changed,current->pickerDraft);y+=36;
 Text(x,y+4,"Palette",MUTED,11);const char* palette[]={"#B84A21","#A68D68","#2E548A","#677D6F","#B0C0CA","#383E45"};for(int i=0;i<6;++i){Rect sample={x+50+i*25.f,y,19,19};std::array<float,4> color{0,0,0,1};ParseHex(palette[i],color);if(Hit("picker.palette."+std::to_string(i),sample)){float a=current->pickerDraft[3];current->pickerDraft=color;current->pickerDraft[3]=a;DraftHSVFromRGB();}Swatch(sample,color);Tip(palette[i]);}y+=31;
 Cursor({x,y});ImGui::Dummy(Physical({1,1}));ImGui::EndChild();scaledDraw=ImGui::GetWindowDrawList();draw=&scaledDraw;Rect footer={window.x,window.y+ph-47,pw,47};Box(footer,IM_COL32(32,39,46,255),0,0);draw->AddLine(footer.Min(),{footer.x+pw,footer.y},BORDER);
 bool cancel=TextButton("picker.cancel",{footer.x+pw-160,footer.y+10,70,27},"Cancel"),apply=TextButton("picker.apply",{footer.x+pw-82,footer.y+10,70,27},"Apply",true);
 if(cancel||ImGui::IsKeyPressed(ImGuiKey_Escape)){current->CancelPicker();ImGui::CloseCurrentPopup();}else if(apply){current->ApplyPicker();ImGui::CloseCurrentPopup();}ImGui::EndPopup();
}
} // namespace

void ApplyTheme(float scale){uiScale=std::max(.5f,scale);ImGui::StyleColorsDark();auto& s=ImGui::GetStyle();s.WindowRounding=5;s.ChildRounding=0;s.FrameRounding=4;s.PopupRounding=6;s.WindowBorderSize=0;s.ChildBorderSize=0;s.FrameBorderSize=1;s.PopupBorderSize=1;s.WindowPadding={0,0};s.FramePadding={8,8};s.ItemSpacing={8,8};s.ItemInnerSpacing={6,4};s.ScrollbarSize=5;s.ScrollbarRounding=3;s.GrabMinSize=7;s.GrabRounding=2;s.DisabledAlpha=.45f;s.Colors[ImGuiCol_Text]=ImColor(TEXT);s.Colors[ImGuiCol_TextDisabled]=ImColor(MUTED);s.Colors[ImGuiCol_WindowBg]=ImColor(BG);s.Colors[ImGuiCol_ChildBg]=ImColor(PANEL);s.Colors[ImGuiCol_PopupBg]=ImColor(RAISED);s.Colors[ImGuiCol_Border]=ImColor(BORDER);s.Colors[ImGuiCol_FrameBg]=ImColor(BG);s.Colors[ImGuiCol_FrameBgHovered]=ImColor(BG);s.Colors[ImGuiCol_FrameBgActive]=ImColor(IM_COL32(27,41,48,255));s.Colors[ImGuiCol_Button]=ImColor(RAISED);s.Colors[ImGuiCol_ButtonHovered]=ImColor(SELECT);s.Colors[ImGuiCol_ButtonActive]=ImColor(SELECT);s.Colors[ImGuiCol_CheckMark]=ImColor(CYAN);s.Colors[ImGuiCol_Header]=ImColor(SELECT);s.Colors[ImGuiCol_HeaderHovered]=ImColor(SELECT);s.Colors[ImGuiCol_HeaderActive]=ImColor(SELECT);s.Colors[ImGuiCol_NavCursor]=ImColor(CYAN);s.Colors[ImGuiCol_ScrollbarBg]=ImColor(PANEL);s.Colors[ImGuiCol_ScrollbarGrab]=ImColor(HOVER);if(scale!=1)s.ScaleAllSizes(scale);}

void DrawInspector(InspectorState& state,float clientWidth,float clientHeight){
 clientWidth/=uiScale;clientHeight/=uiScale;current=&state;state.controls.clear();Rect panel={(clientWidth-318)/2,12,318,std::max(240.f,clientHeight-24)};
 ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(Physical({clientWidth,clientHeight}));ImGui::Begin("InspectorRoot",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBringToFrontOnFocus);scaledDraw=ImGui::GetWindowDrawList();draw=&scaledDraw;Box({0,0,clientWidth,clientHeight},BG,0,0);Box(panel,PANEL,5);Box({panel.x,panel.y,panel.w,38},IM_COL32(26,31,36,255),0,0);draw->AddLine({panel.x,panel.y+38},{panel.x+panel.w,panel.y+38},BORDER);
 const char* tabs[]={"Properties","Stage","Texture"};Glyph icons[]={Glyph::Properties,Glyph::Sun,Glyph::Image};float tw=(panel.w-12)/3;for(int i=0;i<3;++i){Rect r={panel.x+4+i*(tw+2),panel.y+4,tw,30};bool clicked=Hit("tab."+std::to_string(i),r);if(i==state.tab||ImGui::IsItemHovered())Box(r,RAISED,3,0);float labelWidth=TextWidth(tabs[i],11),left=r.x+(r.w-labelWidth-18)/2;DrawGlyph(draw,icons[i],{left,r.y+9},12,i==state.tab?TEXT:MUTED);Text(left+18,r.y+10,tabs[i],i==state.tab?TEXT:MUTED,11);if(i==state.tab)draw->AddRectFilled({r.x+10,panel.y+36},{r.x+r.w-10,panel.y+38},CYAN);if(clicked){state.EndEdit();state.tab=i;}}
 float x=panel.x+12,w=294;Identity(x,panel.y+50,w);const char* shaders[]={"Opaque","Cutout","Transparent","Unlit"};Text(x,panel.y+110,"Shader",MUTED);Combo("shader",{x+62,panel.y+102,w-62,28},state.value.shader,shaders,4);
 Rect search={x,panel.y+140,w,28};Register("search",search);Box(search,BG);DrawGlyph(draw,Glyph::Search,{x+8,search.y+7},14,MUTED);Cursor({x+28,search.y});NextWidth(w-29);ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,Physical({0,8}));ImGui::PushStyleColor(ImGuiCol_FrameBg,IM_COL32(0,0,0,0));ImGui::InputTextWithHint("##search","Search attributes...",state.search,sizeof(state.search));ImGui::PopStyleColor();ImGui::PopStyleVar(2);
 Cursor({panel.x,panel.y+181});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,Physical({12,0}));ImGui::BeginChild("InspectorAttributes",Physical({panel.w,panel.h-215}),ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);ImGui::PopStyleVar();scaledDraw=ImGui::GetWindowDrawList();draw=&scaledDraw;float y=Logical(ImGui::GetCursorScreenPos()).y;if(state.tab==0)Properties(x,y,w);else if(state.tab==1)Stage(x,y,w);else Texture(x,y,w);Cursor({x,y+12});ImGui::Dummy(Physical({1,1}));ImGui::EndChild();scaledDraw=ImGui::GetWindowDrawList();draw=&scaledDraw;
 Rect footer={panel.x,panel.y+panel.h-34,panel.w,34};draw->AddLine(footer.Min(),{footer.x+footer.w,footer.y},BORDER);int count=state.ModifiedCount();ChangedDot(footer.x+14,footer.y+17,count>0);std::string feedback=state.status.empty()?(count?std::to_string(count)+" modified attributes":"No changes"):state.status;Text(footer.x+23,footer.y+11,Ellipsis(feedback,228,11),state.status.find("failed")!=std::string::npos?COPPER:MUTED,11);if(!state.status.empty()&&ImGui::IsMouseHoveringRect(Physical({footer.x+12,footer.y}),Physical({footer.x+244,footer.y+34})))ImGui::SetTooltip("%s",state.status.c_str());
 if(IconButton("undo",{footer.x+footer.w-60,footer.y+3,22,28},Glyph::Undo,!state.undo.empty()||state.editing,"Undo · Ctrl+Z"))state.Undo();if(IconButton("redo",{footer.x+footer.w-34,footer.y+3,22,28},Glyph::Redo,!state.redo.empty(),"Redo · Ctrl+Y"))state.Redo();Picker(clientWidth,clientHeight,panel);ImGui::End();current=nullptr;draw=nullptr;
}
} // namespace atelier::inspector
