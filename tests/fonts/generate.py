# Original geometric fixtures, CC0-1.0. fontTools is a generator-only dependency.
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.t2CharStringPen import T2CharStringPen
from pathlib import Path
root=Path(__file__).parent
points=list(range(32,127))+list(range(0xa0,0x180))+list(range(0x400,0x460))
for ttf,advance,name in [(True,500,"Narrow"),(False,900,"Wide")]:
    fb=FontBuilder(1000,isTTF=ttf)
    names=['.notdef']+['u%04X'%c for c in points]
    fb.setupGlyphOrder(names);fb.setupCharacterMap({c:'u%04X'%c for c in points})
    metrics={g:(advance,0) for g in names};glyphs={}
    for g in names:
        pen=TTGlyphPen(None) if ttf else T2CharStringPen(advance,None)
        if g not in ['u0020','u00A0']:
            pen.moveTo((0,0));pen.lineTo((advance-200,0));pen.lineTo((advance-200,700));pen.lineTo((100,700));pen.lineTo((0,500));pen.closePath()
        glyphs[g]=pen.glyph() if ttf else pen.getCharString()
    if ttf:fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics(metrics);fb.setupHorizontalHeader(ascent=800,descent=-200)
    fb.setupNameTable(dict(familyName='Whisp Fixture',styleName=name,uniqueFontIdentifier='WhispFixture'+name,fullName='Whisp Fixture '+name,psName='WhispFixture-'+name,version='Version 1.0'))
    fb.setupOS2(sTypoAscender=800,sTypoDescender=-200,usWinAscent=800,usWinDescent=200)
    fb.setupPost()
    if not ttf:fb.setupCFF('WhispFixture-'+name,{'FullName':'Whisp Fixture '+name,'FamilyName':'Whisp Fixture','Weight':'Regular'},glyphs,{})
    fb.save(root/(name+('.ttf' if ttf else '.otf')))
