#include "material_ui.h"
#include "material_renderer.h"
#include "ui_icons.h"
#include "atelier_style.h"
#include "ambre_fx.h"
#include "inspector.h"
#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace atelier::material {
namespace {
struct Rect {float x,y,w,h;ImVec2 Min()const{return {x,y};}ImVec2 Max()const{return {x+w,y+h};}};
MaterialLibrary library;Material applied;PreviewSettings preview;PreviewTexture rendered;
ApplyCallback applyCallback=nullptr;void* applyUser=nullptr;std::string projectFile;
ImDrawList* draw=nullptr;ImFont* font=nullptr;float ui=1,leftWidth=230,rightWidth=318,bottomHeight=207;int workspaceRequest=0;
int inspectorTab=0,bottomTab=0,textureSlot=0,libraryFilter=0;bool live=true,stats=true,showFloor=true,help=false,newMaterial=false;
bool sections[7]={true,true,false,false,false,false,false};char librarySearch[128]={},propertySearch[128]={},newName[128]="M_NewMaterial";
ImGuiID editItem=0;Id editMaterial=0;std::string toast;double toastUntil=0;double submitMs=0;
struct Message {std::string text;Severity level;};std::vector<Message> messages;
// colours of the shared Atelier palette (atelier_style.h), updated from the engine theme
const ImU32 &BG=Style().bg,&PANEL=Style().panel,&RAISED=Style().raised,&BORDER=Style().border,&TEXT=Style().text,&MUTED=Style().muted,&CYAN=Style().accent,&SELECT=Style().select;
const ImU32 GREEN=IM_COL32(121,189,143,255),COPPER=IM_COL32(208,148,110,255);
Material& M(){return library.Current();}
ImU32 Color(const std::array<float,4>& c){return ImGui::ColorConvertFloat4ToU32({c[0],c[1],c[2],c[3]});}
ImU32 Alpha(ImU32 c,int a){return (c&0xffffffu)|((ImU32)a<<24);}
void Text(float x,float y,const std::string& text,ImU32 color=TEXT,float size=12){draw->AddText(font,size*ui*Style().textScale,{x,y},color,text.c_str());}
void Center(Rect r,const std::string& text,ImU32 color=TEXT,float size=12){auto z=font->CalcTextSizeA(size*ui*Style().textScale,10000,0,text.c_str());Text(r.x+(r.w-z.x)/2,r.y+(r.h-z.y)/2,text,color,size);}
void Box(Rect r,ImU32 color,float round=3,bool border=false){draw->AddRectFilled(r.Min(),r.Max(),color,round*ui);if(border)draw->AddRect(r.Min(),r.Max(),BORDER,round*ui);}
void IconAt(Icon icon,float x,float y,ImU32 color=MUTED,float size=13){float k=Style().iconScale,o=size*ui*(k-1)/2;DrawIcon(draw,icon,{x-o,y-o},size*ui*k,color);}   // icon size setting, same centre
bool Hit(const char* id,Rect r){ImGui::SetCursorScreenPos(r.Min());return ImGui::InvisibleButton(id,{std::max(1.f,r.w),std::max(1.f,r.h)});}
void Tip(const char* text){if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",text);}
bool Button(const char* id,Rect r,const char* label,Icon icon,bool active=false,bool small=false){bool q=Hit(id,r);ImU32 ic=active?CYAN:TEXT,tc=TEXT;
 if(Style().skin){/* Ambre theme: gradient face, amber halo when active (ambre_fx.h) */auto f=active?fx::Face::Active:ImGui::IsItemHovered()?fx::Face::Hover:fx::Face::Normal;ic=tc=fx::Button(draw,r.Min(),r.Max(),f,ui,4);}else Box(r,active?SELECT:ImGui::IsItemHovered()?Style().hover:RAISED,3,true);
 IconAt(icon,r.x+(small?(r.w/ui-13)/2:8)*ui,r.y+(r.h-13*ui)/2,ic);if(!small)Text(r.x+27*ui,r.y+(r.h-12*ui)/2,label,tc,12);return q;}
bool Tab(const char* id,Rect r,const char* title,Icon icon,bool active){bool q=Hit(id,r);if(Style().skin)fx::TabLine(draw,r.Min(),r.Max(),active,ImGui::IsItemHovered(),ui);/* Ambre: underlined tab */else if(active||ImGui::IsItemHovered())Box(r,active?RAISED:BG,3,true);IconAt(icon,r.x+9*ui,r.y+(r.h-12*ui)/2,active?CYAN:MUTED,12);Text(r.x+28*ui,r.y+(r.h-11*ui)/2,title,active?TEXT:MUTED,11);return q;}
bool Search(const char* id,Rect r,char* value,size_t capacity,const char* hint){ImGui::SetCursorScreenPos(r.Min());ImGui::SetNextItemWidth(r.w);ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{25*ui,5*ui});bool result=ImGui::InputTextWithHint(id,hint,value,capacity);ImGui::PopStyleVar();IconAt(Icon::Search,r.x+8*ui,r.y+8*ui,MUTED,10);return result;}
bool Match(const std::string& text,const char* q){std::string a=text,b=q;for(auto& c:a)c=(char)std::tolower((unsigned char)c);for(auto& c:b)c=(char)std::tolower((unsigned char)c);return b.empty()||a.find(b)!=std::string::npos;}
void Notify(const std::string& text,Severity level=Severity::Info){toast=text;toastUntil=ImGui::GetTime()+3;messages.push_back({text,level});if(messages.size()>120)messages.erase(messages.begin());}
void Commit(){if(editMaterial){library.EndEdit();editMaterial=0;editItem=0;}}
void Select(Id id){Commit();library.Select(id);textureSlot=0;}
void Swatch(Rect r,const Material& m,bool selected=false){
 Box(r,BG,3,true);float radius=std::min(r.w,r.h)*.36f;ImVec2 p={r.x+r.w/2,r.y+r.h/2};ImU32 c=Color(m.baseColor);draw->AddCircleFilled({p.x+1.5f*ui,p.y+2*ui},radius+1*ui,IM_COL32(0,0,0,90),32);draw->AddCircleFilled(p,radius,c,40);
 for(int k=7;k>0;--k){float t=k/8.f;ImU32 shine=IM_COL32(235,224,205,(int)((1-m.roughness)*7+3));draw->AddCircleFilled({p.x-radius*.27f,p.y-radius*.28f},radius*t,shine,32);}draw->AddCircle(p,radius,Alpha(c,200),32,1*ui);
 if(m.pattern==1||m.pattern==5)for(int i=-3;i<=3;++i){float yy=p.y+i*radius*.21f;float half=std::sqrt(std::max(0.f,radius*radius-(yy-p.y)*(yy-p.y)));draw->AddLine({p.x-half*.8f,yy},{p.x+half*.8f,yy},IM_COL32(235,230,215,25));}if(selected)draw->AddRect(r.Min(),r.Max(),CYAN,3*ui,0,1.4f*ui);
}
std::string SafeName(const std::string& name){std::string out=name;for(char& c:out)if(c=='/'||c=='\\'||c==':'||c=='*'||c=='?'||c=='"'||c=='<'||c=='>'||c=='|')c='_';return out.empty()?"Material":out;}
std::filesystem::path Folder(){return std::filesystem::u8path(projectFile).parent_path();}
void SavePreview(){auto path=Folder()/"outputs"/(SafeName(M().name)+"-preview.png");if(ExportPreviewPNG(path))Notify("Preview exported: "+path.filename().u8string());else Notify("Preview export failed: "+GetRendererStatus(),Severity::Error);}
void Save(){Commit();std::string error;if(library.Save(projectFile,&error))Notify("Saved MaterialLibrary.amat");else Notify(error,Severity::Error);}
void Load(){Commit();std::string error;if(library.Load(projectFile,&error))Notify("Material library loaded");else Notify(error,Severity::Error);}
void Apply(){Commit();int errors=0;for(auto d:library.Validate()){if(d.material&&d.material!=M().id)continue;messages.push_back({d.message,d.severity});errors+=d.severity==Severity::Error;}if(errors){Notify("Apply stopped · see diagnostics",Severity::Error);bottomTab=2;return;}applied=M();if(applyCallback)applyCallback(applied,applyUser);Notify(M().name+" published · preview shader ready");}
void ExportMaterial(){Commit();MaterialLibrary exported=library;Id selected=exported.SelectedId();std::vector<Id> other;for(auto& m:exported.Items())if(m.id!=selected)other.push_back(m.id);for(Id id:other)exported.Remove(id);auto path=Folder()/"exports"/(SafeName(M().name)+".amat");std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);std::string error;if(exported.Save(path.u8string(),&error))Notify("Exported "+path.filename().u8string());else Notify(error,Severity::Error);}
void ChooseTexture(int slot){Commit();std::string path=PickTextureFile();if(path.empty())return;library.BeginEdit();M().textureMaps[slot].path=path;M().textureMaps[slot].enabled=true;library.EndEdit();textureSlot=slot;inspectorTab=2;auto texture=GetTexturePreview(path);Notify(texture.id?"Texture assigned":"Texture decoder could not load the selected file",texture.id?Severity::Info:Severity::Error);}
void ResetCamera(){preview.yaw=28;preview.pitch=12;preview.distance=3.7f;}
template<class Enum,class Names>bool Combo(const char* id,Rect r,Enum& value,int count,Names names,Icon icon=Icon::ChevronDown){bool changed=false;if(Button(id,r,names(value),icon))ImGui::OpenPopup(id);if(ImGui::BeginPopup(id)){for(int i=0;i<count;++i)if(ImGui::Selectable(names((Enum)i),value==(Enum)i)){value=(Enum)i;changed=true;}ImGui::EndPopup();}return changed;}
void Header(float width){
 Box({0,0,width,37*ui},Style().bar,0);if(Style().skin){draw->AddLine({0,37*ui-.5f},{width,37*ui-.5f},BORDER);fx::Brand(draw,font,Style().fontBold,{13*ui,6.5f*ui},ui);}else IconAt(Icon::Cube,15*ui,10*ui,CYAN,17);const float sep=(Style().menuX-10)*ui,shift=Style().headerShift*ui;draw->AddLine({sep,7*ui},{sep,30*ui},BORDER);
 const char* names[]={"Project","Edit","View","Tools","Help"};float x=Style().menuX*ui;for(int i=0;i<5;++i){std::string id="materialMenu"+std::to_string(i);Rect r={x,6*ui,Style().menuW*ui,24*ui};if(Hit(id.c_str(),r))ImGui::OpenPopup(id.c_str());if(ImGui::IsItemHovered())Box(r,RAISED);Center(r,names[i],TEXT,11);
  if(ImGui::BeginPopup(id.c_str())){if(i==0){if(ImGui::MenuItem("New material","Ctrl+N"))newMaterial=true;if(ImGui::MenuItem("Open library","Ctrl+O"))Load();if(ImGui::MenuItem("Save library","Ctrl+S"))Save();if(ImGui::MenuItem("Export selected material"))ExportMaterial();ImGui::Separator();ImGui::TextDisabled("%s",projectFile.c_str());}
   if(i==1){if(ImGui::MenuItem("Undo","Ctrl+Z",false,library.CanUndo())){Commit();library.Undo();}if(ImGui::MenuItem("Redo","Ctrl+Y",false,library.CanRedo())){Commit();library.Redo();}if(ImGui::MenuItem("Duplicate material","Ctrl+D")){Commit();library.Duplicate();}if(ImGui::MenuItem("Restore presets")){Commit();library.SeedPresets();Notify("Material presets restored");}}
   if(i==2){ImGui::MenuItem("Live preview",nullptr,&live);ImGui::MenuItem("Preview statistics",nullptr,&stats);ImGui::MenuItem("Floor grid",nullptr,&preview.grid);if(ImGui::MenuItem("Reset camera","F"))ResetCamera();if(ImGui::MenuItem("Reset panel layout")){leftWidth=230;rightWidth=318;bottomHeight=207;}}
   if(i==3){if(ImGui::MenuItem("Apply material","Ctrl+Enter"))Apply();if(ImGui::MenuItem("Export preview PNG"))SavePreview();if(ImGui::MenuItem("Import texture map"))ChooseTexture(textureSlot);if(ImGui::MenuItem("Copy shader source"))ImGui::SetClipboardText(GetShaderSource().c_str());}
   if(i==4){if(ImGui::MenuItem("Controls & preview details","F1"))help=true;ImGui::TextDisabled("WZX Engine · Dear ImGui · DirectX 12");}ImGui::EndPopup();}x+=Style().menuW*ui;
 }
 if(Button("materialWorkspaceScene",{shift+339*ui,7*ui,79*ui,24*ui},"Scene",Icon::Scene))workspaceRequest=1;if(Button("materialWorkspaceGraph",{shift+422*ui,7*ui,96*ui,24*ui},"Graph",Icon::Blueprint))workspaceRequest=2;Button("materialWorkspace",{shift+522*ui,7*ui,117*ui,24*ui},"Materials",Icon::Material,true);
 float mid=width/2-34*ui;if(Button("applyMaterial",{mid,6*ui,87*ui,25*ui},"Apply",Icon::Check))Apply();Tip("Validate and publish this material to the engine callback");
 if(Button("toggleLive",{mid+94*ui,6*ui,108*ui,25*ui},"Live Preview",live?Icon::Play:Icon::Pause,live))live=!live;
 if(Button("saveMaterials",{width-179*ui,6*ui,74*ui,25*ui},"Save",Icon::Save))Save();if(Button("exportMaterialTop",{width-99*ui,6*ui,28*ui,25*ui},"",Icon::Camera,false,true))SavePreview();Tip("Export the actual GPU preview as PNG");IconAt(Icon::Person,width-46*ui,12*ui,TEXT,13);IconAt(Icon::ChevronDown,width-23*ui,15*ui,MUTED,9);
}
void LibraryPanel(Rect r){
 Box(r,PANEL,4,true);IconAt(Icon::Material,r.x+12*ui,r.y+12*ui,CYAN);Text(r.x+33*ui,r.y+12*ui,"Material Library",TEXT,12);float x=r.x+11*ui,y=r.y+47*ui,w=r.w-22*ui;
 Search("##materialLibrarySearch",{x,y,w,28*ui},librarySearch,sizeof(librarySearch),"Search materials...");y+=37*ui;
 if(Button("libraryFilter",{x,y,w-32*ui,24*ui},libraryFilter==0?"All Materials":libraryFilter==1?"Metals":libraryFilter==2?"Dielectrics":"Emissive",Icon::Filter,libraryFilter!=0))ImGui::OpenPopup("materialFilters");if(ImGui::BeginPopup("materialFilters")){const char* filters[]={"All Materials","Metals","Dielectrics","Emissive"};for(int i=0;i<4;++i)if(ImGui::Selectable(filters[i],libraryFilter==i))libraryFilter=i;ImGui::EndPopup();}
 if(Button("newMaterialSmall",{x+w-26*ui,y,26*ui,24*ui},"",Icon::Plus,false,true))newMaterial=true;Tip("Create a new material (Ctrl+N)");y+=41*ui;Text(x,y,"PROJECT / MATERIALS",MUTED,9);Text(x+w-22*ui,y,std::to_string(library.Items().size()),MUTED,9);y+=22*ui;
 float listHeight=std::max(110*ui,r.h-419*ui);ImGui::SetCursorScreenPos({x,y});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("materialLibraryItems",{w,listHeight},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);ImGui::SetWindowFontScale(atelier::Style().layoutScale);ImGui::PopStyleVar();draw=ImGui::GetWindowDrawList();float yy=ImGui::GetCursorScreenPos().y;Id duplicate=0,remove=0;
 for(auto& m:library.Items()){if(!Match(m.name+" "+m.description,librarySearch))continue;if(libraryFilter==1&&m.metallic<.5f)continue;if(libraryFilter==2&&m.metallic>=.5f)continue;if(libraryFilter==3&&m.emissionStrength<=0)continue;Rect row={x,yy,w,57*ui};bool clicked=Hit(("materialItem"+std::to_string(m.id)).c_str(),row);bool hovered=ImGui::IsItemHovered();if(clicked)Select(m.id);bool selected=library.SelectedId()==m.id;if(selected||hovered)Box(row,selected?SELECT:RAISED,3);Swatch({x+5*ui,yy+7*ui,41*ui,41*ui},m);Text(x+56*ui,yy+12*ui,m.name,TEXT,10.5f);Text(x+56*ui,yy+32*ui,m.metallic>=.5f?"Metallic surface":m.shader==Shader::Transparent?"Translucent surface":"PBR surface",selected?CYAN:MUTED,9);
  if(ImGui::BeginPopupContextItem(("materialContext"+std::to_string(m.id)).c_str())){Select(m.id);if(ImGui::MenuItem("Duplicate"))duplicate=m.id;if(ImGui::MenuItem("Export material"))ExportMaterial();if(ImGui::MenuItem("Remove",nullptr,false,library.Items().size()>1))remove=m.id;ImGui::EndPopup();}Tip(m.description.c_str());yy+=61*ui;
 }
 ImGui::SetCursorScreenPos({x,yy});ImGui::Dummy({1,1});ImGui::EndChild();draw=ImGui::GetWindowDrawList();if(duplicate){Commit();library.Duplicate(duplicate);}if(remove){Commit();library.Remove(remove);}
 y+=listHeight+18*ui;draw->AddLine({x,y},{x+w,y},BORDER);y+=18*ui;Text(x,y,"PREVIEW MESH",MUTED,9);y+=24*ui;
 Icon shapes[]={Icon::Material,Icon::Cube,Icon::Rotate,Icon::Grid,Icon::Settings};for(int i=0;i<5;++i){float rowWidth=(w-12*ui)/3;int row=i/3,col=i%3;Rect card={x+col*(rowWidth+6*ui),y+row*63*ui,rowWidth,57*ui};if(Hit(("meshShape"+std::to_string(i)).c_str(),card))preview.shape=(Shape)i;Box(card,preview.shape==(Shape)i?SELECT:BG,3,true);IconAt(shapes[i],card.x+card.w/2-9*ui,card.y+9*ui,preview.shape==(Shape)i?CYAN:MUTED,18);Center({card.x,card.y+35*ui,card.w,16*ui},i==4?"Shader Ball":ShapeName((Shape)i),TEXT,8.5f);Tip(ShapeName((Shape)i));}y+=137*ui;
 Text(x,y,"ENVIRONMENT",MUTED,9);y+=21*ui;Combo("libraryEnvironment",{x,y,w,25*ui},preview.environment,3,EnvironmentName,Icon::World);
}
void PreviewHeader(Rect r){
 Box(r,PANEL,3,true);IconAt(Icon::Material,r.x+12*ui,r.y+11*ui,CYAN);Text(r.x+34*ui,r.y+11*ui,M().name,TEXT,12);Text(r.x+225*ui,r.y+13*ui,"MATERIAL CREATION / PREVIEW",MUTED,8.5f);float x=r.x+r.w-170*ui;
 if(Button("previewStageTab",{x,r.y+6*ui,77*ui,24*ui},"Lighting",Icon::Sun,inspectorTab==1))inspectorTab=1;if(Button("previewInfoToggle",{x+83*ui,r.y+6*ui,26*ui,24*ui},"",Icon::Monitor,stats,true))stats=!stats;if(Button("materialPreviewCapture",{x+115*ui,r.y+6*ui,26*ui,24*ui},"",Icon::Camera,false,true))SavePreview();Tip("Export the preview render");
}
void Preview(Rect r){
 Box(r,BG,3,true);if(live&&preview.turntable)preview.yaw=std::fmod(preview.yaw+ImGui::GetIO().DeltaTime*16,360.f);ClampPreview(preview);
 if(live||!rendered.id){auto before=std::chrono::steady_clock::now();rendered=RenderPreview(M(),preview,std::clamp((int)r.w,240,1600),std::clamp((int)r.h,180,1100));submitMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-before).count();}
 if(rendered.id)draw->AddImage((ImTextureID)(intptr_t)rendered.id,r.Min(),r.Max());else{Center({r.x+20*ui,r.y+r.h/2-20*ui,r.w-40*ui,25*ui},"Preview renderer unavailable",COPPER,14);Text(r.x+22*ui,r.y+r.h/2+20*ui,GetRendererStatus(),MUTED,10);}
 Hit("materialPreviewCanvas",r);if(ImGui::IsItemHovered()){if(ImGui::IsMouseDragging(0)){preview.yaw+=ImGui::GetIO().MouseDelta.x*.36f;preview.pitch+=ImGui::GetIO().MouseDelta.y*.26f;}if(ImGui::GetIO().MouseWheel)preview.distance=std::clamp(preview.distance-ImGui::GetIO().MouseWheel*.25f,1.7f,7.f);if(ImGui::IsMouseDoubleClicked(0))ResetCamera();}Tip("Drag to orbit · wheel to zoom · double click to reset camera");
 float x=r.x+13*ui,y=r.y+13*ui;Box({x,y,125*ui,25*ui},Style().overlay,3,true);IconAt(Icon::Material,x+8*ui,y+7*ui,CYAN,11);Text(x+25*ui,y+7*ui,DebugViewName(preview.debugView),TEXT,10);Box({x+132*ui,y,105*ui,25*ui},Style().overlay,3,true);IconAt(Icon::World,x+140*ui,y+7*ui,MUTED,11);Text(x+157*ui,y+7*ui,EnvironmentName(preview.environment),TEXT,10);
 Text(r.x+18*ui,r.y+r.h-36*ui,M().name,IM_COL32(226,231,236,255),12);Text(r.x+18*ui,r.y+r.h-19*ui,"DIRECTX 12 + DXR  /  PATH TRACED  /  "+std::string(ShapeName(preview.shape)),IM_COL32(160,170,180,255),8.5f);   // over the picture: same colours in light mode
 if(stats){Rect info={r.x+r.w-153*ui,r.y+12*ui,139*ui,93*ui};Box(info,Style().minimap,4,true);Text(info.x+10*ui,info.y+10*ui,"PREVIEW",CYAN,9);char fps[50],cpu[50];std::snprintf(fps,sizeof(fps),"%.0f FPS",ImGui::GetIO().Framerate);std::snprintf(cpu,sizeof(cpu),"%.2f ms",submitMs);Text(info.x+10*ui,info.y+30*ui,"Frame rate",MUTED,9);Text(info.x+78*ui,info.y+30*ui,fps,TEXT,9);Text(info.x+10*ui,info.y+47*ui,"CPU submit",MUTED,9);Text(info.x+78*ui,info.y+47*ui,cpu,TEXT,9);Text(info.x+10*ui,info.y+66*ui,"Resolution",MUTED,9);Text(info.x+76*ui,info.y+66*ui,std::to_string(rendered.width)+"×"+std::to_string(rendered.height),TEXT,8);}
 if(!live)Center({r.x+r.w/2-47*ui,r.y+15*ui,94*ui,23*ui},"PREVIEW PAUSED",COPPER,9);
}
void PreviewControls(Rect r){
 Box(r,PANEL,3,true);float x=r.x+8*ui;if(Button("resetMaterialView",{x,r.y+5*ui,28*ui,24*ui},"",Icon::Home,false,true))ResetCamera();Tip("Reset camera (F)");x+=35*ui;if(Button("turntable",{x,r.y+5*ui,105*ui,24*ui},"Turntable",Icon::Rotate,preview.turntable))preview.turntable=!preview.turntable;x+=112*ui;if(Button("previewGrid",{x,r.y+5*ui,65*ui,24*ui},"Grid",Icon::Grid,preview.grid))preview.grid=!preview.grid;x+=72*ui;
 Combo("materialDebugView",{x,r.y+5*ui,115*ui,24*ui},preview.debugView,6,DebugViewName,Icon::Monitor);x+=123*ui;Combo("materialPreviewShape",{x,r.y+5*ui,116*ui,24*ui},preview.shape,5,ShapeName,Icon::Material);
 Text(r.x+r.w-220*ui,r.y+12*ui,"Exposure",MUTED,10);ImGui::SetCursorScreenPos({r.x+r.w-158*ui,r.y+6*ui});ImGui::SetNextItemWidth(144*ui);ImGui::SliderFloat("##previewExposureQuick",&preview.exposure,-3,3,"%.2f EV");
}
// Attribute Editor (Atelier-Design-AttributEditor, inspector.cpp) bound to the selected material,
// the preview stage and the current texture slot.
inspector::InspectorState attributes;Id attributesMaterial=0;bool attributesEditing=false;
inspector::Document ToDocument(const Material& m,const PreviewSettings& p,int slot){
 inspector::Document d;d.name=m.name;d.shader=(int)m.shader;d.pattern=m.pattern;d.environment=(int)p.environment;d.baseColor=m.baseColor;d.emissionColor={m.emissionColor[0],m.emissionColor[1],m.emissionColor[2],1.f};
 d.roughness=m.roughness;d.metallic=m.metallic;d.normalStrength=m.normalStrength;d.ao=m.ao;d.patternScale=m.patternScale;d.clearcoat=m.clearcoat;d.coatRoughness=m.clearcoatRoughness;d.anisotropy=m.anisotropy;d.sheen=m.sheen;
 d.emissionStrength=m.emissionStrength;d.opacity=m.opacity;d.ior=m.ior;d.uvScale=m.uvScale;d.uvOffset=m.uvOffset;d.uvRotation=m.uvRotation;d.tiling=m.tiling;d.twoSided=m.twoSided;d.castShadows=m.castShadows;d.receiveShadows=m.receiveShadows;
 d.exposure=p.exposure;d.environmentRotation=p.environmentRotation;d.intensity=p.intensity;d.keyIntensity=p.keyIntensity;d.fillIntensity=p.fillIntensity;d.rimIntensity=p.rimIntensity;d.yaw=p.yaw;d.pitch=p.pitch;d.distance=p.distance;d.grid=p.grid;d.shadows=p.shadows;d.turntable=p.turntable;
 const auto& map=m.textureMaps[slot];d.texturePath=map.path;d.textureEnabled=map.enabled;d.textureStrength=map.strength;d.textureChannel=map.channel;inspector::Clamp(d);return d;
}
void FromDocument(const inspector::Document& d,Material& m,PreviewSettings& p,int slot){
 m.name=d.name;m.shader=(Shader)d.shader;m.pattern=d.pattern;p.environment=(Environment)d.environment;m.baseColor=d.baseColor;m.emissionColor={d.emissionColor[0],d.emissionColor[1],d.emissionColor[2]};
 m.roughness=d.roughness;m.metallic=d.metallic;m.normalStrength=d.normalStrength;m.ao=d.ao;m.patternScale=d.patternScale;m.clearcoat=d.clearcoat;m.clearcoatRoughness=d.coatRoughness;m.anisotropy=d.anisotropy;m.sheen=d.sheen;
 m.emissionStrength=d.emissionStrength;m.opacity=d.opacity;m.ior=d.ior;m.uvScale=d.uvScale;m.uvOffset=d.uvOffset;m.uvRotation=d.uvRotation;m.tiling=d.tiling;m.twoSided=d.twoSided;m.castShadows=d.castShadows;m.receiveShadows=d.receiveShadows;
 p.exposure=d.exposure;p.environmentRotation=d.environmentRotation;p.intensity=d.intensity;p.keyIntensity=d.keyIntensity;p.fillIntensity=d.fillIntensity;p.rimIntensity=d.rimIntensity;p.yaw=d.yaw;p.pitch=d.pitch;p.distance=d.distance;p.grid=d.grid;p.shadows=d.shadows;p.turntable=d.turntable;
 auto& map=m.textureMaps[slot];map.path=d.texturePath;map.enabled=d.textureEnabled;map.strength=d.textureStrength;map.channel=d.textureChannel;
}
void Inspector(Rect r){
 inspector::Document doc=ToDocument(M(),preview,textureSlot);
 if(attributesMaterial!=M().id){int tab=attributes.tab;bool keep[10];std::copy(std::begin(attributes.sections),std::end(attributes.sections),keep);attributes=inspector::InspectorState{};attributes.tab=tab;std::copy(std::begin(keep),std::end(keep),attributes.sections);attributes.value=attributes.defaults=doc;attributesMaterial=M().id;}
 else if(!attributes.editing&&!inspector::Equal(doc,attributes.value))attributes.value=doc;   // changed elsewhere: undo, preview camera, texture cards
 attributes.tab=inspectorTab;attributes.textureSlotName=TextureSlotName((TextureSlot)textureSlot);
 inspector::Document before=attributes.value;
 inspector::DrawInspectorPanel(attributes,r.x,r.y,r.w,r.h,ui);draw=ImGui::GetWindowDrawList();
 inspectorTab=attributes.tab;
 if(!inspector::Equal(before,attributes.value)){if(!attributesEditing){Commit();library.BeginEdit();attributesEditing=true;}FromDocument(attributes.value,M(),preview,textureSlot);}
 if(attributesEditing&&!attributes.editing){library.EndEdit();attributesEditing=false;}
}
void TextureCards(Rect r){
 float gap=9*ui,cardWidth=(r.w-26*ui-gap*4)/5,x=r.x+13*ui,y=r.y+9*ui;const ImU32 colors[]={CYAN,GREEN,COPPER,IM_COL32(153,132,224,255),IM_COL32(153,166,180,255)};
 for(int i=0;i<5;++i){auto& map=M().textureMaps[i];Rect card={x+i*(cardWidth+gap),y,cardWidth,r.h-17*ui};bool clicked=Hit(("materialTextureCard"+std::to_string(i)).c_str(),{card.x,card.y,card.w,card.h-27*ui});if(clicked){textureSlot=i;inspectorTab=2;}Box(card,BG,4,true);if(textureSlot==i)draw->AddRect(card.Min(),card.Max(),Alpha(colors[i],220),4*ui,0,1.3f*ui);IconAt(Icon::Material,card.x+10*ui,card.y+9*ui,colors[i],11);Text(card.x+28*ui,card.y+9*ui,TextureSlotName((TextureSlot)i),TEXT,10);
  Rect tile={card.x+9*ui,card.y+31*ui,card.w-18*ui,std::max(34*ui,card.h-76*ui)};Box(tile,RAISED,3);auto image=map.path.empty()?PreviewTexture{}:GetTexturePreview(map.path);if(image.id)draw->AddImage((ImTextureID)(intptr_t)image.id,tile.Min(),tile.Max());else{int nx=7,ny=4;for(int yy=0;yy<ny;++yy)for(int xx=0;xx<nx;++xx){float tw=tile.w/nx,th=tile.h/ny;ImU32 color=i==0?Color(M().baseColor):i==3?IM_COL32(124+xx*3,123+yy*4,232,255):IM_COL32((int)((i==1?M().roughness:i==2?M().metallic:M().ao)*185)+35,(int)((i==1?M().roughness:i==2?M().metallic:M().ao)*185)+35,(int)((i==1?M().roughness:i==2?M().metallic:M().ao)*185)+35,255);if((xx+yy)%2==0)color=Alpha(color,180);draw->AddRectFilled({tile.x+xx*tw,tile.y+yy*th},{tile.x+(xx+1)*tw,tile.y+(yy+1)*th},color);}}
  Text(card.x+10*ui,card.y+card.h-39*ui,map.path.empty()?"Default value":std::filesystem::u8path(map.path).filename().u8string(),MUTED,8.5f);
  if(Button(("textureAssign"+std::to_string(i)).c_str(),{card.x+8*ui,card.y+card.h-27*ui,card.w-16*ui,21*ui},map.path.empty()?"Assign...":"Replace...",Icon::FolderOpen))ChooseTexture(i);
 }
}
void ChannelOverview(Rect r){
 float x=r.x+15*ui,y=r.y+19*ui,cardWidth=std::min(150*ui,std::max(75*ui,(r.w-2*15*ui-152*ui-4*9*ui)/5));const char* titles[]={"Base Color","Roughness","Metallic","Normals","AO"};const ImU32 colors[]={CYAN,GREEN,COPPER,IM_COL32(155,135,226,255),MUTED};float gap=9*ui;Rect result={r.x+r.w-147*ui,y+22*ui,131*ui,83*ui};Box(result,SELECT,4,true);IconAt(Icon::Material,result.x+11*ui,result.y+13*ui,CYAN,20);Text(result.x+39*ui,result.y+14*ui,"Surface",TEXT,13);Text(result.x+11*ui,result.y+51*ui,ShaderName(M().shader),MUTED,9);
 for(int i=0;i<5;++i){Rect card={x+i*(cardWidth+gap),y,cardWidth,98*ui};bool q=Hit(("channelOverview"+std::to_string(i)).c_str(),card);Box(card,BG,4,true);Box({card.x,card.y,card.w,27*ui},Alpha(colors[i],45),4);Text(card.x+10*ui,card.y+9*ui,titles[i],colors[i],10);std::string value;if(i==0){Swatch({card.x+10*ui,card.y+37*ui,38*ui,38*ui},M());Text(card.x+55*ui,card.y+46*ui,"RGBA",MUTED,9);}else{char text[40];std::snprintf(text,sizeof(text),"%.3f",i==1?M().roughness:i==2?M().metallic:i==3?M().normalStrength:M().ao);Center({card.x,card.y+39*ui,card.w,33*ui},text,TEXT,15);}ImVec2 from={card.x+card.w,card.y+79*ui},to={result.x,result.y+68*ui};draw->AddBezierCubic(from,{from.x+22*ui,from.y},{to.x-28*ui,to.y},to,Alpha(colors[i],125),1.4f*ui);draw->AddCircleFilled(from,3*ui,colors[i]);if(q){preview.debugView=(DebugView)(i==0?1:i==1?2:i==2?3:i==3?4:5);Notify("Preview channel: "+std::string(titles[i]));}}
 Text(r.x+15*ui,r.y+r.h-24*ui,"Click a channel to inspect its live GPU output.",MUTED,10);
}
void Output(Rect r){
 float y=r.y+6*ui;ImGui::SetCursorScreenPos({r.x+12*ui,y});ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::BeginChild("materialOutput",{r.w-25*ui,r.h-13*ui},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground);ImGui::SetWindowFontScale(atelier::Style().layoutScale);ImGui::PopStyleVar();draw=ImGui::GetWindowDrawList();y=ImGui::GetCursorScreenPos().y;for(size_t i=0;i<messages.size();++i){auto& message=messages[i];ImU32 color=message.level==Severity::Error?IM_COL32(222,111,123,255):message.level==Severity::Warning?COPPER:GREEN;IconAt(message.level==Severity::Info?Icon::Check:Icon::Info,r.x+14*ui,y+5*ui,color,11);Text(r.x+34*ui,y+4*ui,message.text,message.level==Severity::Info?MUTED:color,10);y+=25*ui;}ImGui::SetCursorScreenPos({r.x+13*ui,y});ImGui::Dummy({1,1});ImGui::EndChild();draw=ImGui::GetWindowDrawList();
}
void ShaderSource(Rect r){ImGui::SetCursorScreenPos({r.x+13*ui,r.y+8*ui});ImGui::BeginChild("materialShaderSource",{r.w-26*ui,r.h-16*ui});ImGui::SetWindowFontScale(atelier::Style().layoutScale);ImGui::TextUnformatted(GetShaderSource().c_str());ImGui::EndChild();}
void Bottom(Rect r){
 Box(r,PANEL,4,true);float x=r.x+5*ui;const char* titles[]={"Texture Maps","Channel Overview","Output","Shader Source"};Icon icons[]={Icon::Material,Icon::Blueprint,Icon::List,Icon::Settings};float widths[]={113,152,78,120};for(int i=0;i<4;++i){if(Tab(("materialBottomTab"+std::to_string(i)).c_str(),{x,r.y+1*ui,widths[i]*ui,27*ui},titles[i],icons[i],bottomTab==i))bottomTab=i;x+=(widths[i]+3)*ui;}
 if(Button("copyMaterialOutput",{r.x+r.w-68*ui,r.y+4*ui,25*ui,23*ui},"",Icon::List,false,true)){std::string text;if(bottomTab==3)text=GetShaderSource();else for(auto& line:messages)text+=line.text+"\n";ImGui::SetClipboardText(text.c_str());Notify("Copied to clipboard");}Tip("Copy shader source or output diagnostics");if(Button("materialValidate",{r.x+r.w-36*ui,r.y+4*ui,25*ui,23*ui},"",Icon::Check,false,true)){Commit();int errors=0;for(auto diagnostic:library.Validate()){messages.push_back({diagnostic.message,diagnostic.severity});errors+=diagnostic.severity==Severity::Error;}Notify(errors?"Material validation found errors":"Material validation passed",errors?Severity::Error:Severity::Info);bottomTab=2;}Tip("Validate material and texture references");Rect body={r.x+2*ui,r.y+32*ui,r.w-4*ui,r.h-35*ui};if(bottomTab==0)TextureCards(body);else if(bottomTab==1)ChannelOverview(body);else if(bottomTab==2)Output(body);else ShaderSource(body);
}
void Popups(){
 if(newMaterial){std::snprintf(newName,sizeof(newName),"M_NewMaterial");ImGui::OpenPopup("Create material");newMaterial=false;}if(ImGui::BeginPopupModal("Create material",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("Create a PBR surface material");ImGui::SetNextItemWidth(260*ui);ImGui::InputText("Name",newName,sizeof(newName));if(ImGui::Button("Create",{118*ui,27*ui})&&*newName){Commit();library.Add(newName);inspectorTab=0;ImGui::CloseCurrentPopup();Notify("New material created");}ImGui::SameLine();if(ImGui::Button("Cancel",{100*ui,27*ui}))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
 if(help){ImGui::OpenPopup("Material Studio controls");help=false;}if(ImGui::BeginPopupModal("Material Studio controls",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){ImGui::Text("ATELIER / MATERIAL CREATION & PREVIEW");ImGui::Separator();ImGui::Text("Choose a material, edit its PBR properties and inspect the render.");ImGui::Text("Drag preview: orbit · Wheel: zoom · F: reset camera");ImGui::Text("Ctrl+N: new · Ctrl+D: duplicate · Ctrl+Z/Y: undo/redo");ImGui::Text("Ctrl+S/O: save/open library · Ctrl+Enter: Apply");ImGui::Text("Space: turntable · F12: capture the full editor");ImGui::Separator();ImGui::Text("The preview is path traced by the engine renderer (DirectX 12 + DXR).");ImGui::Text("Studio environments are procedural, not imported HDR images.");ImGui::Text("Texture maps and patterns are not used by the preview yet.");ImGui::Text("Apply publishes the material through the engine callback.");if(ImGui::Button("Close",{150*ui,27*ui}))ImGui::CloseCurrentPopup();ImGui::EndPopup();}
}
void Keys(){auto& io=ImGui::GetIO();if(io.WantTextInput||ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId))return;if(io.KeyCtrl){if(ImGui::IsKeyPressed(ImGuiKey_S))Save();if(ImGui::IsKeyPressed(ImGuiKey_O))Load();if(ImGui::IsKeyPressed(ImGuiKey_N))newMaterial=true;if(ImGui::IsKeyPressed(ImGuiKey_D)){Commit();library.Duplicate();}if(ImGui::IsKeyPressed(ImGuiKey_Z)){Commit();library.Undo();}if(ImGui::IsKeyPressed(ImGuiKey_Y)){Commit();library.Redo();}if(ImGui::IsKeyPressed(ImGuiKey_Enter))Apply();}else{if(ImGui::IsKeyPressed(ImGuiKey_F))ResetCamera();if(ImGui::IsKeyPressed(ImGuiKey_Space))preview.turntable=!preview.turntable;if(ImGui::IsKeyPressed(ImGuiKey_F1))help=true;}}
void Splitter(const char* id,Rect r,float& value,float min,float max,bool vertical,bool reverse){Hit(id,r);if(ImGui::IsItemHovered()||ImGui::IsItemActive()){ImGui::SetMouseCursor(vertical?ImGuiMouseCursor_ResizeEW:ImGuiMouseCursor_ResizeNS);Box(r,Alpha(CYAN,65),0);}if(ImGui::IsItemActive()){float d=(vertical?ImGui::GetIO().MouseDelta.x:ImGui::GetIO().MouseDelta.y)/ui;value=std::clamp(value+(reverse?-d:d),min,max);}}
bool Flag(const char* key){
#ifdef _WIN32
 char* value=nullptr;size_t length=0;_dupenv_s(&value,&length,key);bool on=value&&*value;std::free(value);return on;
#else
 return std::getenv(key)!=nullptr;
#endif
}
void RenderTests(){
 std::filesystem::create_directories("outputs/material-tests");bool modelPassed=RunMaterialTests("outputs/material-tests","outputs/material-model-test.json");Material test=library.Current();PreviewSettings settings;settings.shape=Shape::Sphere;settings.grid=false;
 struct Variant {const char* name;float roughness;float metallic;std::array<float,4> color;Shape shape;};const Variant variants[]={
 {"copper-smooth",.08f,1,{.63f,.24f,.12f,1},Shape::Sphere},{"copper-rough",.88f,1,{.63f,.24f,.12f,1},Shape::Sphere},{"blue-ceramic",.24f,0,{.06f,.23f,.48f,1},Shape::Sphere},{"copper-torus",.26f,1,{.63f,.24f,.12f,1},Shape::Torus},{"normal-debug",.26f,1,{.63f,.24f,.12f,1},Shape::Cube}};
 std::ofstream report("outputs/material-render-test.json");report<<"{\n  \"model_passed\": "<<(modelPassed?"true":"false")<<",\n  \"captures\": [\n";bool all=true;for(size_t i=0;i<std::size(variants);++i){auto& variant=variants[i];test.roughness=variant.roughness;test.metallic=variant.metallic;test.baseColor=variant.color;settings.shape=variant.shape;settings.debugView=i==4?DebugView::Normals:DebugView::Lit;auto texture=RenderPreview(test,settings,768,640);bool ok=texture.id&&ExportPreviewPNG(std::filesystem::path("outputs/material-tests")/(std::string(variant.name)+".png"));all&=ok;report<<"    {\"name\": \""<<variant.name<<"\", \"saved\": "<<(ok?"true":"false")<<"}"<<(i+1<std::size(variants)?",":"")<<"\n";}report<<"  ],\n  \"render_passed\": "<<(all?"true":"false")<<"\n}\n";Notify(modelPassed&&all?"Material model and GPU captures passed":"Material checks need attention",modelPassed&&all?Severity::Info:Severity::Error);
}
}
void Initialize(const std::string& libraryFile){projectFile=libraryFile;library.SeedPresets();applied=library.Current();preview={};preview.shape=Shape::ShaderBall;rendered={};if(std::filesystem::exists(std::filesystem::u8path(projectFile))){std::string error;if(library.Load(projectFile,&error))applied=library.Current();else Notify(error,Severity::Warning);}messages.push_back({"Material Studio ready · path traced preview (DirectX 12 + DXR)",Severity::Info});messages.push_back({GetRendererStatus(),Severity::Info});if(Flag("ATELIER_MATERIAL_TEST"))RenderTests();}
int Draw(){
 workspaceRequest=0;auto& io=ImGui::GetIO();ui=std::clamp(std::min(io.DisplaySize.x/1600.f,io.DisplaySize.y/900.f),.8f,1.6f)*Style().layoutScale;/* layout size setting */float width=io.DisplaySize.x,height=io.DisplaySize.y;
 ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(io.DisplaySize);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);ImGui::Begin("AtelierMaterialRoot",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse|ImGuiWindowFlags_NoBringToFrontOnFocus);ImGui::PopStyleVar(2);ImGui::SetWindowFontScale(ui*1.1f);font=ImGui::GetFont();draw=ImGui::GetWindowDrawList();Box({0,0,width,height},BG,0);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{11*ui,9*ui});ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,{6*ui,3*ui});ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{6*ui,6*ui});Keys();Header(width);
 float lw=leftWidth*ui,rw=rightWidth*ui,bh=bottomHeight*ui,cx=lw+12*ui,cw=width-cx-rw-12*ui,top=42*ui;
 LibraryPanel({5*ui,top,lw,height-top-10*ui});PreviewHeader({cx,top,cw,35*ui});Rect picture={cx,top+40*ui,cw,height-top-bh-85*ui};Preview(picture);PreviewControls({cx,picture.y+picture.h+5*ui,cw,35*ui});Bottom({cx,height-bh-8*ui,cw,bh});Inspector({width-rw-6*ui,top,rw,height-top-10*ui});
 Splitter("materialLeftSplitter",{lw+5*ui,top,7*ui,height-top-10*ui},leftWidth,200,std::min(345.f,width/ui-850),true,false);Splitter("materialRightSplitter",{width-rw-12*ui,top,6*ui,height-top-10*ui},rightWidth,295,std::min(390.f,width/ui-830),true,true);Splitter("materialBottomSplitter",{cx,height-bh-13*ui,cw,5*ui},bottomHeight,166,std::max(167.f,height/ui-430),false,true);
 Popups();draw=ImGui::GetWindowDrawList();if(toastUntil>ImGui::GetTime()){auto size=font->CalcTextSizeA(11*ui*Style().textScale,10000,0,toast.c_str());Rect r={width/2-size.x/2-16*ui,height-46*ui,size.x+32*ui,28*ui};Box(r,Style().toast,4,true);Center(r,toast,TEXT,11);}ImGui::PopStyleVar(3);ImGui::End();return workspaceRequest;
}
void Shutdown(){Commit();}
const MaterialLibrary& GetLibrary(){return library;}
const Material& GetAppliedMaterial(){return applied;}
const PreviewSettings& GetPreviewSettings(){return preview;}
void SetApplyCallback(ApplyCallback callback,void* user){applyCallback=callback;applyUser=user;}
}
