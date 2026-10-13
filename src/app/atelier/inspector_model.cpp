#include "inspector.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace atelier::inspector {
#ifdef _WIN32
bool ReplaceInspectorFile(const std::filesystem::path&,const std::filesystem::path&);
#endif
const std::vector<ScalarSpec>& ScalarSpecs() {
    static const std::vector<ScalarSpec> specs = {
        {"roughness","Roughness",&Document::roughness,.02f,1,3,""},
        {"metallic","Metallic",&Document::metallic,0,1,3,""},
        {"normalStrength","Normal strength",&Document::normalStrength,0,2,3,""},
        {"ao","Ambient occlusion",&Document::ao,0,1,3,""},
        {"patternScale","Pattern scale",&Document::patternScale,1,40,1,u8"×"},
        {"clearcoat","Clearcoat",&Document::clearcoat,0,1,3,""},
        {"coatRoughness","Coat roughness",&Document::coatRoughness,.02f,1,3,""},
        {"anisotropy","Anisotropy",&Document::anisotropy,0,1,3,""},
        {"sheen","Sheen",&Document::sheen,0,1,3,""},
        {"emissionStrength","Emission strength",&Document::emissionStrength,0,12,2,""},
        {"uvRotation","Rotation",&Document::uvRotation,-180,180,1,u8"°"},
        {"opacity","Opacity",&Document::opacity,0,1,3,""},
        {"ior","Index of refraction",&Document::ior,1,2.5f,2,""},
        {"exposure","Exposure",&Document::exposure,-3,3,2,"EV"},
        {"environmentRotation","Environment rotation",&Document::environmentRotation,-180,180,0,u8"°"},
        {"intensity","Environment intensity",&Document::intensity,.1f,3,2,""},
        {"keyIntensity","Key light",&Document::keyIntensity,0,3,2,""},
        {"fillIntensity","Fill light",&Document::fillIntensity,0,3,2,""},
        {"rimIntensity","Rim light",&Document::rimIntensity,0,3,2,""},
        {"yaw","Orbit",&Document::yaw,-180,180,1,u8"°"},
        {"pitch","Elevation",&Document::pitch,-60,60,1,u8"°"},
        {"distance","Distance",&Document::distance,1.7f,7,2,"m"},
        {"textureStrength","Texture strength",&Document::textureStrength,0,1,3,""}
    };
    return specs;
}
namespace {
struct BoolSpec { const char* id; bool Document::*member; };
const BoolSpec bools[] = {
    {"tiling",&Document::tiling},{"twoSided",&Document::twoSided},
    {"castShadows",&Document::castShadows},{"receiveShadows",&Document::receiveShadows},
    {"grid",&Document::grid},{"shadows",&Document::shadows},
    {"turntable",&Document::turntable},{"textureEnabled",&Document::textureEnabled}
};
struct IntSpec { const char* id; int Document::*member; int max; };
const IntSpec ints[] = {{"shader",&Document::shader,3},{"pattern",&Document::pattern,5},
    {"environment",&Document::environment,2},{"textureChannel",&Document::textureChannel,4}};
float Safe(float v,float lo,float hi,float fallback) { return std::isfinite(v)?std::clamp(v,lo,hi):fallback; }
std::string Escape(const std::string& s) {
    std::ostringstream out; out << '"';
    for(unsigned char c:s) switch(c) {
        case '"':out<<"\\\"";break;case '\\':out<<"\\\\";break;
        case '\n':out<<"\\n";break;case '\r':out<<"\\r";break;case '\t':out<<"\\t";break;
        default: if(c<32) out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec; else out<<char(c);
    }
    out << '"'; return out.str();
}
struct Json {
    enum Type { Null,Number,String,Boolean,Array,Object } type=Null;
    double number=0; std::string string; bool boolean=false;
    std::vector<Json> array; std::map<std::string,Json> object;
};
class Parser {
    const std::string& s; size_t p=0;
    void Space(){while(p<s.size()&&std::isspace(static_cast<unsigned char>(s[p])))++p;}
    [[noreturn]] void Fail(const char* reason){throw std::runtime_error(std::string(reason)+" at byte "+std::to_string(p));}
    unsigned Code(){if(p+4>s.size())Fail("Incomplete Unicode escape"); unsigned v=0; for(int i=0;i<4;++i){char c=s[p++];int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(n<0)Fail("Invalid Unicode escape");v=v*16+n;}return v;}
    void Utf8(std::string& r,unsigned c){if(c<128)r+=char(c);else if(c<2048){r+=char(0xc0|(c>>6));r+=char(0x80|(c&63));}else if(c<65536){r+=char(0xe0|(c>>12));r+=char(0x80|((c>>6)&63));r+=char(0x80|(c&63));}else{r+=char(0xf0|(c>>18));r+=char(0x80|((c>>12)&63));r+=char(0x80|((c>>6)&63));r+=char(0x80|(c&63));}}
    std::string Str(){if(p==s.size()||s[p++]!='"')Fail("Expected string");std::string r;while(p<s.size()){unsigned char c=s[p++];if(c=='"')return r;if(c<32)Fail("Control character in string");if(c!='\\'){r+=char(c);continue;}if(p==s.size())Fail("Incomplete escape");char e=s[p++];switch(e){case '"':r+='"';break;case '\\':r+='\\';break;case '/':r+='/';break;case 'b':r+='\b';break;case 'f':r+='\f';break;case 'n':r+='\n';break;case 'r':r+='\r';break;case 't':r+='\t';break;case 'u':{unsigned code=Code();if(code>=0xd800&&code<=0xdbff){if(p+2>s.size()||s[p++]!='\\'||s[p++]!='u')Fail("Missing low surrogate");unsigned low=Code();if(low<0xdc00||low>0xdfff)Fail("Invalid low surrogate");code=0x10000+((code-0xd800)<<10)+low-0xdc00;}else if(code>=0xdc00&&code<=0xdfff)Fail("Unexpected low surrogate");Utf8(r,code);break;}default:Fail("Invalid escape");}}Fail("Unterminated string");}
    Json Value(int depth){if(depth>16)Fail("JSON nesting limit");Space();if(p==s.size())Fail("Missing value");Json v;char c=s[p];if(c=='"'){v.type=Json::String;v.string=Str();return v;}if(c=='{'||c=='['){bool object=c=='{';++p;v.type=object?Json::Object:Json::Array;Space();char end=object?'}':']';if(p<s.size()&&s[p]==end){++p;return v;}while(true){Space();if(object){std::string key=Str();Space();if(p==s.size()||s[p++]!=':')Fail("Expected colon");Json element=Value(depth+1);if(!v.object.emplace(key,std::move(element)).second)Fail("Duplicate object key");}else v.array.push_back(Value(depth+1));Space();if(p==s.size())Fail("Unclosed container");if(s[p]==end){++p;return v;}if(s[p++]!=',')Fail("Expected comma");}}
        if(s.compare(p,4,"true")==0){p+=4;v.type=Json::Boolean;v.boolean=true;return v;}if(s.compare(p,5,"false")==0){p+=5;v.type=Json::Boolean;return v;}if(s.compare(p,4,"null")==0){p+=4;return v;}
        size_t start=p;if(s[p]=='-')++p;if(p==s.size()||s[p]<'0'||s[p]>'9')Fail("Invalid number");if(s[p]=='0')++p;else while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;if(p<s.size()&&s[p]=='.'){++p;size_t d=p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;if(d==p)Fail("Missing fraction");}if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t d=p;while(p<s.size()&&std::isdigit(static_cast<unsigned char>(s[p])))++p;if(d==p)Fail("Missing exponent");}v.type=Json::Number;v.number=std::strtod(s.substr(start,p-start).c_str(),nullptr);if(!std::isfinite(v.number)||std::abs(v.number)>3.4e38)Fail("Non-finite number");return v;
    }
public: explicit Parser(const std::string& text):s(text){} Json Parse(){if(s.size()>1024*1024)Fail("File too large");Json v=Value(0);Space();if(p!=s.size())Fail("Trailing data");return v;}
};
const Json* Field(const Json& j,const char* key){auto i=j.object.find(key);return i==j.object.end()?nullptr:&i->second;}
void Type(const Json& j,Json::Type t,const char* name){if(j.type!=t)throw std::runtime_error(std::string("Wrong type for ")+name);}
template<size_t N>void ArrayRead(const Json& j,const char* name,std::array<float,N>& out){const Json* v=Field(j,name);if(!v)return;Type(*v,Json::Array,name);if(v->array.size()!=N)throw std::runtime_error(std::string("Wrong component count for ")+name);for(size_t i=0;i<N;++i){Type(v->array[i],Json::Number,name);out[i]=float(v->array[i].number);}}
void RGBtoHSV(const std::array<float,4>& rgb,std::array<float,3>& hsv){float r=rgb[0],g=rgb[1],b=rgb[2],mx=std::max({r,g,b}),mn=std::min({r,g,b}),d=mx-mn;float h=0;if(d>0){if(mx==r)h=(g-b)/d;else if(mx==g)h=2+(b-r)/d;else h=4+(r-g)/d;h/=6;if(h<0)h+=1;}hsv={h,mx==0?0:d/mx,mx};}
}
void Clamp(Document& d) {
    const Document fallback;
    for(const auto& spec:ScalarSpecs())d.*spec.member=Safe(d.*spec.member,spec.minimum,spec.maximum,fallback.*spec.member);
    for(const auto& spec:ints)d.*spec.member=std::clamp(d.*spec.member,0,spec.max);
    for(size_t i=0;i<4;++i){d.baseColor[i]=Safe(d.baseColor[i],0,1,fallback.baseColor[i]);d.emissionColor[i]=Safe(d.emissionColor[i],0,1,fallback.emissionColor[i]);}
    d.emissionColor[3]=1;
    for(int i=0;i<2;++i){d.uvScale[i]=Safe(d.uvScale[i],.01f,40,1);d.uvOffset[i]=Safe(d.uvOffset[i],-20,20,0);}
    if(d.name.empty())d.name=fallback.name;
    if(d.name.size()>256){size_t end=256;while(end&&((static_cast<unsigned char>(d.name[end])&0xc0)==0x80))--end;d.name.resize(end);}
}
bool Equal(const Document& a,const Document& b){
    if(a.name!=b.name||a.texturePath!=b.texturePath||a.baseColor!=b.baseColor||a.emissionColor!=b.emissionColor||a.uvScale!=b.uvScale||a.uvOffset!=b.uvOffset)return false;
    for(const auto& s:ScalarSpecs())if(a.*s.member!=b.*s.member)return false;
    for(const auto& s:bools)if(a.*s.member!=b.*s.member)return false;
    for(const auto& s:ints)if(a.*s.member!=b.*s.member)return false;
    return true;
}
std::string ToJson(const Document& d){
    std::ostringstream o;o.imbue(std::locale::classic());o<<std::setprecision(9)<<"{\n  \"format\": \"atelier-inspector\",\n  \"version\": 1,\n  \"values\": {\n    \"name\": "<<Escape(d.name)<<",\n    \"texturePath\": "<<Escape(d.texturePath);
    for(const auto& s:ScalarSpecs())o<<",\n    "<<Escape(s.id)<<": "<<d.*s.member;
    for(const auto& s:ints)o<<",\n    "<<Escape(s.id)<<": "<<d.*s.member;
    for(const auto& s:bools)o<<",\n    "<<Escape(s.id)<<": "<<(d.*s.member?"true":"false");
    auto array=[&](const char* id,const auto& a){o<<",\n    "<<Escape(id)<<": [";for(size_t i=0;i<a.size();++i){if(i)o<<", ";o<<a[i];}o<<"]";};
    array("baseColor",d.baseColor);array("emissionColor",d.emissionColor);array("uvScale",d.uvScale);array("uvOffset",d.uvOffset);o<<"\n  }\n}\n";return o.str();
}
bool FromJson(const std::string& text,Document& result,std::string& error){try{
    Json root=Parser(text).Parse();Type(root,Json::Object,"root");const Json* format=Field(root,"format"),*version=Field(root,"version"),*values=Field(root,"values");
    if(!format||format->type!=Json::String||format->string!="atelier-inspector"||!version||version->type!=Json::Number||version->number!=1||!values)throw std::runtime_error("Unsupported inspector document");Type(*values,Json::Object,"values");Document d;
    for(const char* id:{"name","texturePath"})if(const Json* v=Field(*values,id)){Type(*v,Json::String,id);if(v->string.size()>32768||v->string.find('\0')!=std::string::npos)throw std::runtime_error("Invalid string length or embedded zero");if(std::string(id)=="name")d.name=v->string;else d.texturePath=v->string;}
    for(const auto& s:ScalarSpecs())if(const Json* v=Field(*values,s.id)){Type(*v,Json::Number,s.id);d.*s.member=float(v->number);}
    for(const auto& s:bools)if(const Json* v=Field(*values,s.id)){Type(*v,Json::Boolean,s.id);d.*s.member=v->boolean;}
    for(const auto& s:ints)if(const Json* v=Field(*values,s.id)){Type(*v,Json::Number,s.id);if(v->number!=std::floor(v->number)||v->number<0||v->number>s.max)throw std::runtime_error(std::string("Invalid enumeration ")+s.id);d.*s.member=int(v->number);}
    ArrayRead(*values,"baseColor",d.baseColor);ArrayRead(*values,"emissionColor",d.emissionColor);ArrayRead(*values,"uvScale",d.uvScale);ArrayRead(*values,"uvOffset",d.uvOffset);Clamp(d);result=std::move(d);error.clear();return true;
}catch(const std::exception& e){error=e.what();return false;}}
bool ParseHex(const std::string& text,std::array<float,4>& out){if(text.size()!=7||text[0]!='#')return false;std::array<float,4> c=out;for(int k=0;k<3;++k){int byte=0;for(int j=0;j<2;++j){char v=text[1+k*2+j];int n=v>='0'&&v<='9'?v-'0':v>='a'&&v<='f'?v-'a'+10:v>='A'&&v<='F'?v-'A'+10:-1;if(n<0)return false;byte=byte*16+n;}c[k]=byte/255.f;}out=c;return true;}
std::string Hex(const std::array<float,4>& color){std::ostringstream out;out<<'#'<<std::uppercase<<std::hex<<std::setfill('0');for(int i=0;i<3;++i)out<<std::setw(2)<<int(std::round(Safe(color[i],0,1,0)*255));return out.str();}
void InspectorState::BeginEdit(const Document& before){if(!editing){editing=true;editBefore=before;}}
bool InspectorState::EndEdit(){if(!editing)return false;Document before=editBefore;editing=false;return Commit(before);}
bool InspectorState::Commit(const Document& before){Clamp(value);if(Equal(value,before))return false;undo.push_back(before);if(undo.size()>128)undo.erase(undo.begin());redo.clear();status.clear();return true;}
bool InspectorState::Undo(){EndEdit();CancelPicker();if(undo.empty())return false;redo.push_back(value);value=undo.back();undo.pop_back();status="Undo";return true;}
bool InspectorState::Redo(){EndEdit();CancelPicker();if(redo.empty())return false;undo.push_back(value);value=redo.back();redo.pop_back();status="Redo";return true;}
bool InspectorState::Reset(const std::string& id){EndEdit();Document before=value;if(id=="all")value=defaults;else if(id=="baseColor")value.baseColor=defaults.baseColor;else if(id=="emissionColor")value.emissionColor=defaults.emissionColor;else if(id=="uvScale")value.uvScale=defaults.uvScale;else if(id=="uvOffset")value.uvOffset=defaults.uvOffset;else if(id=="name")value.name=defaults.name;else if(id=="texturePath")value.texturePath=defaults.texturePath;else{for(const auto& s:ScalarSpecs())if(id==s.id)value.*s.member=defaults.*s.member;for(const auto& s:bools)if(id==s.id)value.*s.member=defaults.*s.member;for(const auto& s:ints)if(id==s.id)value.*s.member=defaults.*s.member;}return Commit(before);}
int InspectorState::ModifiedCount() const {int n=value.name!=defaults.name;n+=value.texturePath!=defaults.texturePath;n+=value.baseColor!=defaults.baseColor;n+=value.emissionColor!=defaults.emissionColor;n+=value.uvScale!=defaults.uvScale;n+=value.uvOffset!=defaults.uvOffset;for(const auto& s:ScalarSpecs())n+=value.*s.member!=defaults.*s.member;for(const auto& s:bools)n+=value.*s.member!=defaults.*s.member;for(const auto& s:ints)n+=value.*s.member!=defaults.*s.member;return n;}
bool InspectorState::Save(const std::string& path){EndEdit();try{std::filesystem::path destination=std::filesystem::u8path(path),temp=destination;temp+=".tmp";{std::ofstream file(temp,std::ios::binary|std::ios::trunc);if(!file)throw std::runtime_error("Cannot open save file");file<<ToJson(value);file.flush();if(!file)throw std::runtime_error("Cannot write save file");}std::error_code error;std::filesystem::rename(temp,destination,error);if(error){
#ifdef _WIN32
        // Replace an existing file atomically, keeping the old file on failure.
        if(!ReplaceInspectorFile(temp,destination))throw std::runtime_error("Cannot replace save file");
#else
        throw std::runtime_error("Cannot replace save file");
#endif
    }storagePath=path;status="Saved";return true;}catch(const std::exception& e){status=std::string("Save failed: ")+e.what();return false;}}
bool InspectorState::Load(const std::string& path){try{std::ifstream file(std::filesystem::u8path(path),std::ios::binary);if(!file)throw std::runtime_error("Cannot open document");std::string text((std::istreambuf_iterator<char>(file)),{}),error;Document next;if(!FromJson(text,next,error))throw std::runtime_error(error);EndEdit();CancelPicker();Document before=value;value=std::move(next);Commit(before);storagePath=path;status="Loaded";return true;}catch(const std::exception& e){status=std::string("Load failed: ")+e.what();return false;}}
void InspectorState::OpenPicker(int target){EndEdit();pickerTarget=target==1?1:0;pickerDraft=pickerTarget?value.emissionColor:value.baseColor;pickerOriginal=pickerDraft;RGBtoHSV(pickerDraft,pickerHSV);pickerOpen=true;requestPicker=true;}
void InspectorState::ApplyPicker(){if(!pickerOpen)return;Document before=value;if(pickerTarget)value.emissionColor=pickerDraft;else value.baseColor=pickerDraft;Commit(before);pickerOpen=false;requestPicker=false;}
void InspectorState::CancelPicker(){pickerDraft=pickerOriginal;pickerOpen=false;requestPicker=false;}
const ControlRect* InspectorState::FindControl(const std::string& id)const{auto it=std::find_if(controls.begin(),controls.end(),[&](const ControlRect& r){return r.id==id;});return it==controls.end()?nullptr:&*it;}

bool RunModelTests(const std::string& reportPath){std::vector<std::pair<std::string,bool>> checks;auto test=[&](const char* n,bool b){checks.emplace_back(n,b);};Document initial,roundtrip;std::string error;
    test("json defaults round trip",FromJson(ToJson(initial),roundtrip,error)&&Equal(initial,roundtrip));
    Document unicode=initial;unicode.name=u8"M_Émail_铜_\"test\"";unicode.texturePath=u8"C:\\textures\\été\\café.png";test("unicode and escaped paths round trip",FromJson(ToJson(unicode),roundtrip,error)&&Equal(unicode,roundtrip));
    Document untouched=roundtrip;test("malformed JSON rejected atomically",!FromJson("{\"format\":",roundtrip,error)&&Equal(untouched,roundtrip));
    test("wrong property type rejected",!FromJson("{\"format\":\"atelier-inspector\",\"version\":1,\"values\":{\"roughness\":true}}",roundtrip,error));
    test("overflow and infinity rejected",!FromJson("{\"format\":\"atelier-inspector\",\"version\":1,\"values\":{\"roughness\":1e999}}",roundtrip,error));
    test("duplicate JSON keys rejected",!FromJson("{\"format\":\"atelier-inspector\",\"version\":1,\"values\":{\"roughness\":0.2,\"roughness\":0.9}}",roundtrip,error));
    test("invalid enumeration rejected",!FromJson("{\"format\":\"atelier-inspector\",\"version\":1,\"values\":{\"shader\":9}}",roundtrip,error));
    test("color component count checked",!FromJson("{\"format\":\"atelier-inspector\",\"version\":1,\"values\":{\"baseColor\":[0,1]}}",roundtrip,error));
    std::array<float,4> color{0,0,0,.42f};test("hex parsing keeps alpha separate",ParseHex("#B84A21",color)&&Hex(color)=="#B84A21"&&color[3]==.42f);auto oldColor=color;test("invalid hex cannot change color",!ParseHex("#GG0011",color)&&color==oldColor);
    InspectorState state;state.BeginEdit(state.value);for(int i=0;i<40;++i)state.value.roughness=.3f+i*.01f;state.EndEdit();test("one undo for continuous drag",state.undo.size()==1&&state.value.roughness>.6f);test("undo restores complete document",state.Undo()&&Equal(state.value,state.defaults));test("redo restores gesture",state.Redo()&&state.value.roughness>.6f);
    state.Reset("roughness");test("per property reset and modified count",state.value.roughness==.27f&&state.ModifiedCount()==0);test("no history entry for unchanged value",!state.Commit(state.value));
    state.OpenPicker();state.pickerDraft={0,.5f,1,.2f};state.CancelPicker();test("cancel color draft restores original data",state.value.baseColor==initial.baseColor);
    state.OpenPicker();state.pickerDraft={0,.5f,1,.2f};state.ApplyPicker();test("apply color commits all channels once",state.value.baseColor[3]==.2f&&state.value.opacity==1&&state.undo.size()==3);state.Undo();test("undo color preserves unrelated attributes",state.value.baseColor==initial.baseColor&&state.value.opacity==1);
    Document clamped=initial;clamped.roughness=-5;clamped.opacity=9;clamped.uvScale={-1,99};clamped.ior=0;Clamp(clamped);test("numeric and vector limits",clamped.roughness==.02f&&clamped.opacity==1&&clamped.uvScale[0]==.01f&&clamped.uvScale[1]==40&&clamped.ior==1);
    state.value.shader=0;state.value.opacity=.3f;state.value.textureEnabled=false;state.value.textureStrength=.4f;Clamp(state.value);test("inactive properties keep authored values",state.value.opacity==.3f&&state.value.textureStrength==.4f);
    state.value=unicode;std::filesystem::path report=std::filesystem::u8path(reportPath);std::error_code ec;if(report.has_parent_path())std::filesystem::create_directories(report.parent_path(),ec);auto saved=report.parent_path()/std::filesystem::u8path(u8"roundtrip-é.inspector.json");bool savedOk=state.Save(saved.u8string());state.value=initial;test("native file save load round trip",savedOk&&state.Load(saved.u8string())&&Equal(state.value,unicode));test("existing save file replacement",state.Save(saved.u8string()));
    int passed=0;for(const auto& c:checks)passed+=c.second;std::ofstream out(report,std::ios::binary|std::ios::trunc);out<<"{\n  \"suite\": \"inspector-model\",\n  \"passed\": "<<passed<<", \"total\": "<<checks.size()<<",\n  \"checks\": [\n";for(size_t i=0;i<checks.size();++i)out<<"    {\"name\": "<<Escape(checks[i].first)<<", \"passed\": "<<(checks[i].second?"true":"false")<<"}"<<(i+1<checks.size()?",":"")<<"\n";out<<"  ]\n}\n";return bool(out)&&passed==int(checks.size());}
} // namespace atelier::inspector

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
namespace atelier::inspector {
bool ReplaceInspectorFile(const std::filesystem::path& from,const std::filesystem::path& to){return MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;}
}
#endif
