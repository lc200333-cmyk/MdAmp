#include "modern_ui.h"
#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace mdamp {
namespace {
// Original vector implementation of observed geometry; no skin raster resources.
constexpr COLORREF white = RGB(255,255,255), black = RGB(0,0,0);
constexpr RECT volume{183,142,264,155};
RECT Shift(RECT r, int x) { OffsetRect(&r,x,0); return r; }
RECT EqPanel(int w) { const int x=(w-320)/2; return {x,159,x+320,261}; }
RECT EqBand(int w,int b) { const int x=(w-320)/2+133+b*18; return {x,166,x+12,243}; }
RECT Preamp(int w) { return Shift({81,166,94,243},(w-320)/2); }
RECT Pan(int w) { return Shift({12,173,73,186},(w-320)/2); }
RECT Seek(int w) { return {9,108,w-96,122}; }
RECT TitleButton(int w,int i) { const int x=w-39+i*12; return {x,3,x+10,13}; }
RECT Tab(int w,int i) {
    const int x=(w-320)/2;
    constexpr int starts[]{0,88,153}, ends[]{88,153,249};
    return {x+starts[i],259,x+ends[i],274};
}
COLORREF Blend(COLORREF a,COLORREF b,int weight) {
    const auto channel=[weight](int first,int second) { return (first*(255-weight)+second*weight)/255; };
    return RGB(channel(GetRValue(a),GetRValue(b)),channel(GetGValue(a),GetGValue(b)),channel(GetBValue(a),GetBValue(b)));
}
void Fill(HDC dc,RECT r,COLORREF c) {
    const HBRUSH brush=CreateSolidBrush(c); FillRect(dc,&r,brush); DeleteObject(brush);
}
void Line(HDC dc,int x1,int y1,int x2,int y2,COLORREF c) {
    const HPEN pen=CreatePen(PS_SOLID,1,c); const HGDIOBJ old=SelectObject(dc,pen);
    MoveToEx(dc,x1,y1,nullptr); LineTo(dc,x2,y2); SelectObject(dc,old); DeleteObject(pen);
}
void Border(HDC dc,RECT r,COLORREF c) {
    const HBRUSH brush=CreateSolidBrush(c); FrameRect(dc,&r,brush); DeleteObject(brush);
}
void Gradient(HDC dc,RECT r,COLORREF top,COLORREF bottom) {
    const int h=static_cast<int>(r.bottom-r.top);
    for(int y=0;y<h;++y) Fill(dc,{r.left,r.top+y,r.right,r.top+y+1},Blend(top,bottom,y*255/std::max(1,h-1)));
}
void ShapeGradient(HDC dc,RECT r,COLORREF top,COLORREF bottom,COLORREF outline,int radius,bool ellipse=false) {
    const int saved=SaveDC(dc);
    // A path respects the DC's logical-to-device transform at 100% and 200%.
    BeginPath(dc);
    if(ellipse) Ellipse(dc,r.left,r.top,r.right,r.bottom); else RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    EndPath(dc); SelectClipPath(dc,RGN_AND); Gradient(dc,r,top,bottom); RestoreDC(dc,saved);
    const HPEN pen=CreatePen(PS_SOLID,1,outline);
    const HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    if(ellipse) Ellipse(dc,r.left,r.top,r.right,r.bottom); else RoundRect(dc,r.left,r.top,r.right,r.bottom,radius,radius);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);
}
void Label(HDC dc,RECT r,const wchar_t* text,COLORREF c,int size=10,UINT align=DT_LEFT,
    bool bold=false,const wchar_t* face=L"Tahoma") {
    const HFONT font=CreateFontW(-size,0,0,0,bold?FW_BOLD:FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,face);
    const HGDIOBJ old=SelectObject(dc,font); const int mode=SetBkMode(dc,TRANSPARENT);
    const COLORREF oldColor=SetTextColor(dc,c);
    DrawTextW(dc,text,-1,&r,align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
    SetTextColor(dc,oldColor); SetBkMode(dc,mode); SelectObject(dc,old); DeleteObject(font);
}
void Capsule(HDC dc,RECT r,const wchar_t* text,const Palette& p,bool selected=false,int size=9,bool enabled=true) {
    ShapeGradient(dc,r,Blend(p.panel,white,180),Blend(p.panel,black,45),Blend(p.dim,black,100),12);
    ShapeGradient(dc,{r.left+2,r.top+2,r.right-2,r.bottom-2},white,
        selected?Blend(p.panel,p.accent,80):p.panel,p.panel,9);
    Label(dc,r,text,enabled?(selected?p.accent:p.text):p.dim,size,DT_CENTER,selected);
}
void PolygonInk(HDC dc,POINT* points,int count,COLORREF ink) {
    const HPEN pen=CreatePen(PS_SOLID,1,ink); const HBRUSH brush=CreateSolidBrush(ink);
    const HGDIOBJ oldPen=SelectObject(dc,pen),oldBrush=SelectObject(dc,brush); Polygon(dc,points,count);
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(brush); DeleteObject(pen);
}
RECT TransportRect(int i) {
    constexpr int centers[]{18,48,77,107,137}; const int radius=i==0||i==4?11:14;
    return {centers[i]-radius,140-radius,centers[i]+radius+1,140+radius+1};
}
void Transport(HDC dc,RECT r,Action action,const Palette& p,bool selected=false) {
    ShapeGradient(dc,r,Blend(p.panel,white,150),Blend(p.background,black,75),p.dim,0,true);
    ShapeGradient(dc,{r.left+2,r.top+2,r.right-2,r.bottom-2},white,p.panel,Blend(p.dim,white,80),0,true);
    const int cx=static_cast<int>((r.left+r.right)/2),cy=static_cast<int>((r.top+r.bottom)/2);
    const COLORREF ink=selected?p.accent:p.text;
    if(action==Action::Pause) {
        Fill(dc,{cx-3,cy-4,cx-1,cy+4},ink); Fill(dc,{cx+1,cy-4,cx+3,cy+4},ink);
    } else if(action==Action::Stop) Fill(dc,{cx-4,cy-4,cx+4,cy+4},ink);
    else if(action==Action::Previous||action==Action::Next) {
        const int direction=action==Action::Previous?-1:1;
        for(int offset:{-2,3}) {
            const int center=cx+offset;
            POINT pts[]{{center-direction*2,cy-4},{center-direction*2,cy+4},{center+direction*2,cy}};
            PolygonInk(dc,pts,3,ink);
        }
    } else {
        POINT pts[]{{cx-3,cy-5},{cx-3,cy+5},{cx+5,cy}}; PolygonInk(dc,pts,3,ink);
    }
}
void Horizontal(HDC dc,RECT r,float value,const Palette& p,bool centered=false,bool showThumb=true) {
    const int left=static_cast<int>(r.left),right=static_cast<int>(r.right),cy=static_cast<int>((r.top+r.bottom)/2);
    ShapeGradient(dc,{left,cy-2,right,cy+3},p.dim,white,p.dim,5);
    const float position=centered?(value+1.0f)*.5f:value;
    const int x=left+static_cast<int>(std::clamp(position,0.0f,1.0f)*static_cast<float>(right-left-15));
    if(showThumb) ShapeGradient(dc,{x,cy-4,x+15,cy+5},white,Blend(p.panel,black,20),p.text,8);
}
void Vertical(HDC dc,RECT r,float db,const Palette& p) {
    const int x=static_cast<int>((r.left+r.right)/2),top=static_cast<int>(r.top),bottom=static_cast<int>(r.bottom);
    ShapeGradient(dc,{x-4,top,x+4,bottom},Blend(p.dim,black,45),white,p.dim,7);
    Line(dc,x,top+4,x,bottom-4,p.text);
    const int y=top+7+static_cast<int>((12.0f-std::clamp(db,-12.0f,12.0f))/24.0f*static_cast<float>(bottom-top-17));
    ShapeGradient(dc,{x-4,y-8,x+5,y+9},white,p.panel,p.text,7); Line(dc,x-2,y-4,x-2,y+4,white);
}
// Dotted digits generated locally rather than extracted from a skin font.
void DottedTime(HDC dc,int x,int y,std::wstring text,COLORREF ink) {
    constexpr unsigned char digits[10][5]{
        {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},{0x42,0x61,0x51,0x49,0x46},
        {0x21,0x41,0x45,0x4b,0x31},{0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},
        {6,0x49,0x49,0x29,0x1e}};
    if(text.size()==4&&text[1]==L':') text.insert(text.begin(),L'0');
    for(wchar_t c:text) {
        if(c>=L'0'&&c<=L'9') {
            for(int col=0;col<5;++col) for(int row=0;row<7;++row)
                if((digits[c-L'0'][col]&(1<<row))!=0)
                    Fill(dc,{x+col*2,y+row*2,x+col*2+1,y+row*2+1},ink);
            x+=12;
        } else if(c==L':') {
            Fill(dc,{x,y+4,x+1,y+5},ink); Fill(dc,{x,y+10,x+1,y+11},ink); x+=4;
        } else if(c==L'-') { Fill(dc,{x,y+6,x+7,y+7},ink); x+=9; }
    }
}
void Add(std::vector<HitRegion>& hits,RECT r,Action action) { hits.push_back({r,action}); }
void Title(HDC dc, RECT bounds, const wchar_t* title, const Palette& p, bool playerControls) {
    const int left=static_cast<int>(bounds.left),top=static_cast<int>(bounds.top),right=static_cast<int>(bounds.right);
    Gradient(dc,{left+2,top+2,right-2,top+15},RGB(16,23,39),RGB(72,83,110));
    Border(dc,{left,top,right,top+17},RGB(19,24,37)); Line(dc,left+3,top+1,right-3,top+1,white);
    const int middle=(left+right)/2;
    const int gap=title&&*title?std::min(75,static_cast<int>(wcslen(title))*4+8):0;
    Line(dc,left+20,top+6,middle-gap,top+6,RGB(182,193,216));
    Line(dc,left+20,top+7,middle-gap,top+7,RGB(27,34,51));
    Line(dc,middle+gap,top+6,right-49,top+6,RGB(182,193,216));
    Line(dc,middle+gap,top+7,right-49,top+7,RGB(27,34,51));
    Label(dc,{middle-gap,top+1,middle+gap,top+14},title,RGB(173,205,247),10,DT_CENTER);
    Capsule(dc,{left+4,top+3,left+14,top+13},L"~",p,false,8);
    constexpr const wchar_t* glyphs[]{L"-",L"=",L"x"};
    for (int i = playerControls ? 0 : 2; i < 3; ++i) {
        RECT r=TitleButton(right-left,i); OffsetRect(&r,left,top);
        Gradient(dc,r,white,p.panel); Border(dc,r,p.text); Label(dc,r,glyphs[i],p.text,8,DT_CENTER);
    }
}
void TabButton(HDC dc,RECT r,const wchar_t* text,const Palette& p,bool active) {
    POINT pts[]{{r.left,r.top},{r.right,r.top},{r.right-6,r.bottom},{r.left+7,r.bottom}};
    PolygonInk(dc,pts,4,Blend(p.panel,black,70));
    const int saved=SaveDC(dc); BeginPath(dc); Polygon(dc,pts,4); EndPath(dc); SelectClipPath(dc,RGN_AND);
    Gradient(dc,{r.left+1,r.top,r.right-1,r.bottom-1},white,active?p.panel:Blend(p.panel,black,15));
    RestoreDC(dc,saved); Label(dc,r,text,p.text,8,DT_CENTER,active);
}
}

int PlayerLogicalWidth(int width) { return std::clamp(width,354,4096); }
int PlayerLogicalHeight(bool shade,bool drawer) { return shade?17:drawer?280:164; }
std::array<RECT,5> PlayerMenuRects() {
    return {{{6,17,39,33},{39,17,73,33},{73,17,123,33},{123,17,162,33},{162,17,201,33}}};
}
Palette ThemePalette(int theme) {
    static constexpr Palette palettes[]{
        {RGB(184,185,193),RGB(220,221,228),RGB(24,29,41),RGB(44,85,166),RGB(105,111,126)},
        {RGB(35,40,49),RGB(63,72,85),RGB(228,234,243),RGB(74,180,255),RGB(104,119,136)},
        {RGB(56,62,50),RGB(107,119,90),RGB(239,244,213),RGB(195,231,80),RGB(78,88,64)},
        {RGB(72,49,43),RGB(136,101,81),RGB(255,239,215),RGB(255,164,60),RGB(103,74,63)},
        {RGB(62,51,80),RGB(110,94,140),RGB(246,233,255),RGB(221,154,255),RGB(88,75,107)},
        {RGB(183,195,192),RGB(214,226,221),RGB(27,58,51),RGB(17,132,109),RGB(103,133,124)}
    };
    const int count=static_cast<int>(std::size(palettes)); return palettes[((theme%count)+count)%count];
}
std::vector<std::wstring> ThemeNames() {
    return {L"Modern Silver",L"Midnight Blue",L"Phosphor",L"Amber",L"Violet",L"Sea Glass"};
}
std::vector<HitRegion> PlayerHits(bool shade,bool drawer,int tab,int requestedWidth) {
    const int w=PlayerLogicalWidth(requestedWidth),panel=(w-320)/2;
    std::vector<HitRegion> hits;
    Add(hits,TitleButton(w,0),Action::Minimize); Add(hits,TitleButton(w,1),Action::Shade); Add(hits,TitleButton(w,2),Action::Close);
    constexpr Action transport[]{Action::Previous,Action::Play,Action::Pause,Action::Stop,Action::Next};
    if(shade) {
        for(int i=0;i<5;++i) Add(hits,{17+i*16,2,32+i*16,15},transport[i]);
        Add(hits,{w-135,2,w-49,15},Action::Volume); return hits;
    }
    for(int i=0;i<5;++i) Add(hits,TransportRect(i),transport[i]);
    Add(hits,Seek(w),Action::Seek); Add(hits,volume,Action::Volume);
    Add(hits,{w-40,54,w-17,72},Action::Shuffle); Add(hits,{w-40,76,w-17,94},Action::Repeat);
    Add(hits,{w-91,106,w-64,125},Action::Open); Add(hits,{w-64,106,w-39,125},Action::TogglePlaylist);
    Add(hits,{w-38,106,w-12,125},Action::ToggleLibrary); Add(hits,{w-139,47,w-56,80},Action::Visualize);
    Add(hits,{w-77,134,w-43,155},Action::Preferences); Add(hits,{w-40,134,w-9,155},Action::ToggleDrawer);
    if(!drawer) return hits;
    Add(hits,{w-78,264,w-38,278},Action::ToggleDrawer);
    Add(hits,Tab(w,0),Action::DrawerEq); Add(hits,Tab(w,1),Action::DrawerOptions); Add(hits,Tab(w,2),Action::DrawerThemes);
    if(tab==0) {
        Add(hits,Pan(w),Action::Pan); Add(hits,Preamp(w),Action::Preamp);
        Add(hits,Shift({13,207,46,221},panel),Action::EqToggle);
        // AUTO is visibly disabled rather than pretending automatic preset support.
        Add(hits,Shift({13,235,65,249},panel),Action::EqPreset);
        for(int i=0;i<10;++i) Add(hits,EqBand(w,i),static_cast<Action>(static_cast<int>(Action::Band0)+i));
    } else if(tab==1) {
        Add(hits,Shift({8,174,157,197},panel),Action::Preferences); Add(hits,Shift({163,174,312,197},panel),Action::TogglePlaylist);
        Add(hits,Shift({8,205,105,228},panel),Action::Shuffle); Add(hits,Shift({112,205,209,228},panel),Action::Repeat);
        Add(hits,Shift({216,205,312,228},panel),Action::EqAuto);
    } else {
        Add(hits,Shift({12,173,308,207},panel),Action::ThemeSelect); Add(hits,Shift({163,225,207,246},panel),Action::ThemePrevious);
        Add(hits,Shift({212,225,256,246},panel),Action::ThemeNext); Add(hits,Shift({261,225,308,246},panel),Action::ThemeSelect);
    }
    return hits;
}
void PaintFrame(HDC dc, RECT bounds, const wchar_t* title, int theme, bool playerControls) {
    const Palette p=ThemePalette(theme);
    Gradient(dc,bounds,Blend(p.panel,white,60),p.background); Border(dc,bounds,RGB(24,29,40));
    Border(dc,{bounds.left+1,bounds.top+1,bounds.right-1,bounds.bottom-1},white);
    Border(dc,{bounds.left+3,bounds.top+17,bounds.right-3,bounds.bottom-3},p.dim);
    Title(dc, bounds, title, p, playerControls);
}
void PaintPlayer(HDC dc,const PlayerView& view) {
    const int w=PlayerLogicalWidth(view.width),h=PlayerLogicalHeight(view.shade,view.drawer);
    const Palette p=ThemePalette(view.theme);
    PaintFrame(dc, {0, 0, w, h}, view.shade ? L"" : L"MDAMP", view.theme, true);
    if(view.shade) {
        constexpr const wchar_t* symbols[]{L"|<",L">",L"||",L"[]",L">|"};
        for(int i=0;i<5;++i) Capsule(dc,{17+i*16,2,32+i*16,15},symbols[i],p,false,7);
        Label(dc,{103,1,w-140,16},view.title.c_str(),RGB(196,215,248),9);
        Horizontal(dc,{w-135,2,w-49,15},view.volume,p); return;
    }
    Gradient(dc,{1,17,w-1,34},white,p.panel);
    constexpr const wchar_t* menuNames[]{L"File",L"Play",L"Options",L"View",L"Help"};
    const auto menus=PlayerMenuRects();
    for(size_t i=0;i<menus.size();++i) Label(dc,menus[i],menuNames[i],p.text,10);
    Line(dc,1,33,w-1,33,p.dim);
    ShapeGradient(dc,{1,33,w-1,160},Blend(p.panel,white,120),p.background,p.dim,16);
    Line(dc,5,128,w-5,128,white); Gradient(dc,{4,130,w-4,155},Blend(p.panel,white,120),p.background);
    const RECT display{8,41,w-48,101};
    ShapeGradient(dc,{display.left-2,display.top-2,display.right+2,display.bottom+2},RGB(38,45,61),white,RGB(38,45,61),13);
    ShapeGradient(dc,display,RGB(36,67,125),RGB(4,12,34),RGB(4,12,34),12);
    const int saved=SaveDC(dc);
    BeginPath(dc); RoundRect(dc,display.left+2,display.top+2,display.right-2,display.bottom-2,10,10);
    EndPath(dc); SelectClipPath(dc,RGN_AND);
    for(int y=44;y<99;y+=3) for(int x=12;x<w-51;x+=3) SetPixelV(dc,x,y,y<80?RGB(54,77,121):RGB(25,43,84));
    Gradient(dc,{11,45,w-51,52},RGB(70,91,133),RGB(42,65,111)); DottedTime(dc,36,55,view.time,RGB(196,217,255));
    Label(dc,{106,48,146,61},L"KBPS",RGB(180,196,224),7); Label(dc,{154,48,188,61},L"kHz",RGB(180,196,224),7);
    Label(dc,{195,48,224,61},view.eqEnabled?L"EQ":L"",RGB(180,196,224),7);
    Label(dc,{107,62,w-147,76},view.paused?L"PAUSED":view.info.c_str(),RGB(126,148,191),7);
    std::wstring title=view.title;
    std::transform(title.begin(),title.end(),title.begin(),[](wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
    Label(dc,{13,79,w-53,99},title.c_str(),RGB(185,206,246),11,DT_CENTER,false,L"Lucida Console");
    for(int i=0;i<32;++i) {
        const float level=std::clamp(view.spectrum[static_cast<size_t>(i)],0.0f,1.0f);
        const int bar=static_cast<int>(level*25.0f);
        for(int y=73;y>73-bar;y-=3) Fill(dc,{w-135+i*2,y,w-134+i*2,y+1},RGB(183,211,255));
    }
    RestoreDC(dc,saved);
    Capsule(dc,{w-38,34,w-6,48},L"v",p,false,8);
    Capsule(dc,{w-40,54,w-17,72},L"S",p,view.shuffle,9);
    Capsule(dc,{w-40,76,w-17,94},view.repeat<0?L"1":L"R",p,view.repeat!=0,9);
    Horizontal(dc,Seek(w),view.progress,p,false,view.progress>0.0f);
    Capsule(dc,{w-91,106,w-64,125},L"",p);
    const int ejectX=w-78; POINT eject[]{{ejectX,111},{ejectX-4,116},{ejectX+4,116}};
    PolygonInk(dc,eject,3,p.text); Line(dc,ejectX-4,119,ejectX+5,119,p.text);
    Capsule(dc,{w-64,106,w-39,125},L"PL",p,view.playlistVisible,9); Capsule(dc,{w-38,106,w-12,125},L"ML",p,view.libraryVisible,9);
    constexpr Action actions[]{Action::Previous,Action::Play,Action::Pause,Action::Stop,Action::Next};
    for(int i=0;i<5;++i) Transport(dc,TransportRect(i),actions[i],p,
        (i==1&&view.playing&&!view.paused)||(i==2&&view.paused)||(i==3&&!view.playing));
    Capsule(dc,{162,137,179,155},L"",p);
    POINT speaker[]{{167,144},{170,144},{173,141},{173,151},{170,148},{167,148}}; PolygonInk(dc,speaker,6,p.text);
    Horizontal(dc,volume,view.volume,p);
    for(int i=0;i<7;++i) Line(dc,195+i*10,136,197+i*10,134,white);
    Label(dc,{w-77,135,w-43,149},L"CONFIG",p.text,7,DT_CENTER); Line(dc,w-69,151,w-50,151,p.text);
    Capsule(dc,{w-38,138,w-13,155},view.drawer?L"-":L"+",p,false,12);
    if(!view.drawer) return;
    ShapeGradient(dc,{7,159,w-7,279},p.background,Blend(p.panel,white,30),p.dim,10);
    const RECT panel=EqPanel(w); const int x=static_cast<int>(panel.left);
    ShapeGradient(dc,panel,Blend(p.panel,white,130),p.panel,p.dim,10); Line(dc,x+1,160,x+318,160,white);
    if(view.tab==0) {
        for(int i=0;i<14;++i) Fill(dc,{x+7+i*4,166,x+9+i*4,171},p.dim);
        Horizontal(dc,Pan(w),view.pan,p,true); Label(dc,Shift({5,184,74,205},x),L"L    BALANCE    R",p.dim,7,DT_CENTER);
        Capsule(dc,Shift({13,207,46,221},x),L"ON",p,view.eqEnabled,8);
        Capsule(dc,Shift({13,221,52,235},x),L"AUTO",p,false,8,false);
        Capsule(dc,Shift({13,235,65,249},x),L"PRESETS",p,false,8);
        Vertical(dc,Preamp(w),view.preamp,p);
        Label(dc,Shift({91,168,125,181},x),L"+12dB",p.dim,7,DT_CENTER);
        Label(dc,Shift({95,201,125,212},x),L"+0dB",p.dim,7,DT_CENTER);
        Label(dc,Shift({91,232,125,244},x),L"-12dB",p.dim,7,DT_CENTER);
        Label(dc,Shift({75,244,99,256},x),L"PREAMP",p.dim,6,DT_CENTER);
        constexpr const wchar_t* frequencies[]{L"60",L"170",L"310",L"600",L"1K",L"3K",L"6K",L"12K",L"14K",L"16K"};
        for(int i=0;i<10;++i) {
            Vertical(dc,EqBand(w,i),view.eq[static_cast<size_t>(i)],p);
            Label(dc,Shift({131+i*18,244,148+i*18,256},x),frequencies[i],p.dim,7,DT_CENTER);
        }
    } else if(view.tab==1) {
        Capsule(dc,Shift({8,174,157,197},x),L"Preferences...  Ctrl+P",p,false,10);
        Capsule(dc,Shift({163,174,312,197},x),L"Playlist Editor  Alt+E",p,view.playlistVisible,10);
        Capsule(dc,Shift({8,205,105,228},x),L"Shuffle  S",p,view.shuffle,10);
        Capsule(dc,Shift({112,205,209,228},x),L"Repeat  R",p,view.repeat!=0,10);
        Capsule(dc,Shift({216,205,312,228},x),L"Flat EQ",p,false,10);
        Label(dc,Shift({10,235,310,252},x),L"MdAmp playback, audio and DSP settings",p.dim,9,DT_CENTER);
    } else {
        const auto names=ThemeNames(); const size_t index=static_cast<size_t>(((view.theme%6)+6)%6);
        Capsule(dc,Shift({12,173,308,207},x),names[index].c_str(),p,true,14);
        Label(dc,Shift({12,226,158,246},x),L"MdAmp original palettes",p.dim,9);
        Capsule(dc,Shift({163,225,207,246},x),L"<",p); Capsule(dc,Shift({212,225,256,246},x),L">",p);
        Capsule(dc,Shift({261,225,308,246},x),L"Select",p);
    }
    TabButton(dc,Tab(w,0),L"EQUALIZER",p,view.tab==0); TabButton(dc,Tab(w,1),L"OPTIONS",p,view.tab==1);
    TabButton(dc,Tab(w,2),L"COLOR THEMES",p,view.tab==2); Capsule(dc,{w-78,264,w-38,278},L"^",p,false,9);
}
}
