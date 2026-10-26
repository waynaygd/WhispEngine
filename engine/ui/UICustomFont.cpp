#define NOMINMAX
#include "UIFont.h"
#include <stdexcept>
#include <algorithm>
#include <cmath>
#ifdef _WIN32
#include <Windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#endif
namespace ui {
std::vector<unsigned> FontCoverage(){std::vector<unsigned> points;for(unsigned c=32;c<=0x24f;++c)points.push_back(c);for(unsigned c=0x400;c<=0x52f;++c)points.push_back(c);for(unsigned c=0x2000;c<=0x206f;++c)points.push_back(c);points.push_back(0x20ac);return points;}
FontAtlas RasterizeFontFile(const std::filesystem::path& path,float size,const std::vector<unsigned>& coverage){
#ifdef _WIN32
 using Microsoft::WRL::ComPtr;auto check=[](HRESULT h,const char* text){if(FAILED(h))throw std::runtime_error(std::string(text)+" (HRESULT "+std::to_string(static_cast<unsigned>(h))+")");};
 if(!std::isfinite(size)||size<8||size>96||coverage.size()>2048)throw std::runtime_error("Font raster size/coverage limit exceeded");
 ComPtr<IDWriteFactory> factory;check(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED,__uuidof(IDWriteFactory),reinterpret_cast<IUnknown**>(factory.GetAddressOf())),"DirectWrite factory");
 ComPtr<IDWriteFontFile> file;check(factory->CreateFontFileReference(path.c_str(),nullptr,&file),"Font file");BOOL supported=false;DWRITE_FONT_FILE_TYPE type;DWRITE_FONT_FACE_TYPE faceType;UINT32 faces=0;check(file->Analyze(&supported,&type,&faceType,&faces),"Font analysis");if(!supported||faces!=1)throw std::runtime_error("Unsupported/corrupt font or collection; expected a single TTF/OTF face");
 auto* files=file.Get();ComPtr<IDWriteFontFace> face;check(factory->CreateFontFace(faceType,1,&files,0,DWRITE_FONT_SIMULATIONS_NONE,&face),"Font face");DWRITE_FONT_METRICS metrics{};face->GetMetrics(&metrics);if(!metrics.designUnitsPerEm)throw std::runtime_error("Invalid font metrics");
 FontAtlas out;out.height=size;float unit=size/metrics.designUnitsPerEm;out.ascent=metrics.ascent*unit;out.descent=metrics.descent*unit;out.lineHeight=(metrics.ascent+metrics.descent+metrics.lineGap)*unit;
 out.texture.width=out.texture.height=1024;out.texture.pixels.resize(1024*1024*4,0);for(std::size_t i=0;i<out.texture.pixels.size();i+=4)out.texture.pixels[i]=out.texture.pixels[i+1]=out.texture.pixels[i+2]=255;out.texture.pixels[3]=255;
 int x=2,y=2,row=0;
 for(auto point:coverage){if(point>0x10ffff||(point>=0xd800&&point<=0xdfff))continue;UINT32 code=point;UINT16 index=0;check(face->GetGlyphIndices(&code,1,&index),"Glyph lookup");if(!index)continue;DWRITE_GLYPH_METRICS gm{};check(face->GetDesignGlyphMetrics(&index,1,&gm,FALSE),"Glyph metrics");FLOAT advance=gm.advanceWidth*unit;DWRITE_GLYPH_OFFSET offset{};DWRITE_GLYPH_RUN run{face.Get(),size,1,&index,&advance,&offset,FALSE,0};ComPtr<IDWriteGlyphRunAnalysis> analysis;check(factory->CreateGlyphRunAnalysis(&run,1,nullptr,DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC,DWRITE_MEASURING_MODE_NATURAL,0,0,&analysis),"Glyph analysis");RECT bounds{};check(analysis->GetAlphaTextureBounds(DWRITE_TEXTURE_CLEARTYPE_3x1,&bounds),"Glyph bounds");int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;if(w<0||h<0||w>256||h>256)throw std::runtime_error("Glyph exceeds 256 px bounds");if(x+w+1>=1024){x=2;y+=row+2;row=0;}if(y+h+1>=1024)throw std::runtime_error("Font atlas capacity exceeded (1024x1024, one page)");
  if(w&&h){std::vector<BYTE> alpha(std::size_t(w)*h*3);check(analysis->CreateAlphaTexture(DWRITE_TEXTURE_CLEARTYPE_3x1,&bounds,alpha.data(),UINT32(alpha.size())),"Glyph bitmap");for(int iy=0;iy<h;++iy)for(int ix=0;ix<w;++ix){auto p=(std::size_t(iy)*w+ix)*3;out.texture.pixels[((y+iy)*1024+x+ix)*4+3]=BYTE((unsigned(alpha[p])+alpha[p+1]+alpha[p+2])/3);}}
  Glyph g;g.advance=advance;g.pixels={float(bounds.left),float(bounds.top),float(w),float(h)};g.uv={x/1024.f,y/1024.f,(x+w)/1024.f,(y+h)/1024.f};out.glyphs[point]=g;x+=w+2;row=std::max(row,h);
 }
 if(out.glyphs.empty())throw std::runtime_error("Font contains none of the requested glyph coverage");return out;
#else
 throw std::runtime_error("Custom font rasterization currently requires Windows DirectWrite");
#endif
}
}
