#version 300 es
precision highp float;
in  vec2 v_texCoord;
uniform sampler2D texBackground;
uniform sampler2D texForeground;
uniform float     opacity;
uniform int       u_blendMode;  // BlendMode enum value (0=NORMAL)
out vec4 fragColor;

// ── Blend mode implementations ───────────────────────────────────────────
vec3 blendMultiply   (vec3 b,vec3 f){return b*f;}
vec3 blendScreen     (vec3 b,vec3 f){return 1.0-(1.0-b)*(1.0-f);}
vec3 blendOverlay    (vec3 b,vec3 f){return mix(2.0*b*f,1.0-2.0*(1.0-b)*(1.0-f),step(0.5,b));}
vec3 blendSoftLight  (vec3 b,vec3 f){return mix(b-(1.0-2.0*f)*b*(1.0-b),b+(2.0*f-1.0)*(sqrt(b)-b),step(0.5,f));}
vec3 blendHardLight  (vec3 b,vec3 f){return blendOverlay(f,b);}
vec3 blendColorDodge (vec3 b,vec3 f){return min(b/max(1.0-f,0.001),1.0);}
vec3 blendColorBurn  (vec3 b,vec3 f){return 1.0-min((1.0-b)/max(f,0.001),1.0);}
vec3 blendDarken     (vec3 b,vec3 f){return min(b,f);}
vec3 blendLighten    (vec3 b,vec3 f){return max(b,f);}
vec3 blendDifference (vec3 b,vec3 f){return abs(b-f);}
vec3 blendExclusion  (vec3 b,vec3 f){return b+f-2.0*b*f;}
vec3 blendAdd        (vec3 b,vec3 f){return min(b+f,1.0);}

float luma(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}
vec3 setLum(vec3 c,float l){float d=l-luma(c);c+=d;float mn=min(c.r,min(c.g,c.b)),mx=max(c.r,max(c.g,c.b));if(mn<0.0)c=l+(c-l)*l/max(l-mn,0.001);if(mx>1.0)c=l+(c-l)*(1.0-l)/max(mx-l,0.001);return c;}

vec3 applyBlend(int mode, vec3 bg, vec3 fg) {
    if      (mode==1)  return blendMultiply(bg,fg);
    else if (mode==2)  return blendScreen(bg,fg);
    else if (mode==3)  return blendOverlay(bg,fg);
    else if (mode==4)  return blendSoftLight(bg,fg);
    else if (mode==5)  return blendHardLight(bg,fg);
    else if (mode==6)  return blendColorDodge(bg,fg);
    else if (mode==7)  return blendColorBurn(bg,fg);
    else if (mode==8)  return blendDarken(bg,fg);
    else if (mode==9)  return blendLighten(bg,fg);
    else if (mode==10) return blendDifference(bg,fg);
    else if (mode==11) return blendExclusion(bg,fg);
    else if (mode==14) return setLum(fg, luma(bg));       // COLOR
    else if (mode==15) return setLum(bg, luma(fg));       // LUMINOSITY
    else if (mode==16) return blendAdd(bg,fg);
    else               return fg;  // NORMAL + HSL modes default to fg
}

void main() {
    vec4  bg      = texture(texBackground, v_texCoord);
    vec4  fg      = texture(texForeground, v_texCoord);
    float fgAlpha = fg.a * opacity;

    vec3 blendedRgb;
    if (u_blendMode == 0) {
        // NORMAL: standard Porter-Duff SRC_OVER
        blendedRgb = fg.rgb * fgAlpha + bg.rgb * (1.0 - fgAlpha);
    } else {
        vec3 blended = applyBlend(u_blendMode, bg.rgb, fg.rgb);
        blendedRgb   = mix(bg.rgb, blended, fgAlpha);
    }
    fragColor = vec4(blendedRgb, max(bg.a, fgAlpha));
}
