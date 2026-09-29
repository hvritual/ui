#include "hosts/linux/ui/style.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"STYLE_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
enum{T_BG=1,T_FG=2,T_SURFACE=3,T_ACCENT=4,T_PRESSED=5,T_DISABLED=6};
static PocketStyle atom_style(unsigned field,PocketStyleAtom atom){
 PocketStyle s;memset(&s,0,sizeof(s));s.set_mask=POCKET_STYLE_BIT(field);s.fields[field]=atom;return s;
}
int main(void){
 PocketStyleRuntime rt={0};PocketStyleRuntimeConfig cfg={.theme_capacity=4,.token_capacity=32,.rule_capacity=32};
 CHECK(pocket_style_runtime_init(&rt,&cfg)==POCKET_STYLE_OK);
 PocketThemeToken light[]={{T_BG,0xffffff},{T_FG,0x202020},{T_SURFACE,0xf0f0f0},{T_ACCENT,0x0060aa},{T_PRESSED,0xd0d0d0},{T_DISABLED,0xaaaaaa}};
 PocketThemeToken dark[]={{T_BG,0x101010},{T_FG,0xf0f0f0},{T_SURFACE,0x303030},{T_ACCENT,0x55aaff},{T_PRESSED,0x505050},{T_DISABLED,0x444444}};
 PocketStyle base={0};base.set_mask=POCKET_STYLE_BIT(POCKET_STYLE_BACKGROUND)|POCKET_STYLE_BIT(POCKET_STYLE_FOREGROUND)|POCKET_STYLE_BIT(POCKET_STYLE_FONT_ID)|POCKET_STYLE_BIT(POCKET_STYLE_FONT_SIZE);
 base.fields[POCKET_STYLE_BACKGROUND]=pocket_style_token(T_BG);base.fields[POCKET_STYLE_FOREGROUND]=pocket_style_token(T_FG);
 base.fields[POCKET_STYLE_FONT_ID]=pocket_style_literal(1);base.fields[POCKET_STYLE_FONT_SIZE]=pocket_style_literal(22);
 PocketThemeDefinition l={1,base,light,6},d={2,base,dark,6};CHECK(pocket_style_add_theme(&rt,&l)==POCKET_STYLE_OK);CHECK(pocket_style_add_theme(&rt,&d)==POCKET_STYLE_OK);
 PocketStyleRule surface={100,0,10,atom_style(POCKET_STYLE_BACKGROUND,pocket_style_token(T_SURFACE))};
 PocketStyle radius=atom_style(POCKET_STYLE_RADIUS,pocket_style_literal(12));radius.set_mask|=POCKET_STYLE_BIT(POCKET_STYLE_SPACING);radius.fields[POCKET_STYLE_SPACING]=pocket_style_literal(8);
 PocketStyleRule shape={100,0,20,radius};
 PocketStyleRule focused={100,POCKET_STATE_FOCUSED,30,atom_style(POCKET_STYLE_BORDER_COLOR,pocket_style_token(T_ACCENT))};
 PocketStyleRule pressed={100,POCKET_STATE_PRESSED,40,atom_style(POCKET_STYLE_BACKGROUND,pocket_style_token(T_PRESSED))};
 PocketStyle disabledStyle=atom_style(POCKET_STYLE_BACKGROUND,pocket_style_token(T_DISABLED));disabledStyle.set_mask|=POCKET_STYLE_BIT(POCKET_STYLE_OPACITY);disabledStyle.fields[POCKET_STYLE_OPACITY]=pocket_style_literal(128);
 PocketStyleRule disabled={100,POCKET_STATE_DISABLED,50,disabledStyle};
 CHECK(pocket_style_add_rule(&rt,&surface)==POCKET_STYLE_OK);CHECK(pocket_style_add_rule(&rt,&shape)==POCKET_STYLE_OK);
 CHECK(pocket_style_add_rule(&rt,&focused)==POCKET_STYLE_OK);CHECK(pocket_style_add_rule(&rt,&pressed)==POCKET_STYLE_OK);CHECK(pocket_style_add_rule(&rt,&disabled)==POCKET_STYLE_OK);
 PocketResolvedStyle parent={0};parent.set_mask=POCKET_STYLE_BIT(POCKET_STYLE_FOREGROUND)|POCKET_STYLE_BIT(POCKET_STYLE_FONT_SIZE);
 parent.fields[POCKET_STYLE_FOREGROUND]=0x123456;parent.fields[POCKET_STYLE_FONT_SIZE]=26;
 PocketResolvedStyle out;CHECK(pocket_style_resolve(&rt,100,POCKET_STATE_FOCUSED|POCKET_STATE_PRESSED,&parent,&out)==POCKET_STYLE_OK);
 CHECK(out.fields[POCKET_STYLE_BACKGROUND]==0xd0d0d0&&out.fields[POCKET_STYLE_FOREGROUND]==0x123456&&out.fields[POCKET_STYLE_FONT_SIZE]==26);
 CHECK(out.fields[POCKET_STYLE_BORDER_COLOR]==0x0060aa&&out.fields[POCKET_STYLE_RADIUS]==12&&out.fields[POCKET_STYLE_SPACING]==8);
 CHECK(pocket_style_resolve(&rt,100,POCKET_STATE_PRESSED|POCKET_STATE_DISABLED,NULL,&out)==POCKET_STYLE_OK);
 CHECK(out.fields[POCKET_STYLE_BACKGROUND]==0xaaaaaa&&out.fields[POCKET_STYLE_OPACITY]==128);
 PocketStyleRule missing={200,0,1,atom_style(POCKET_STYLE_BACKGROUND,pocket_style_token(999))};CHECK(pocket_style_add_rule(&rt,&missing)==POCKET_STYLE_OK);
 CHECK(pocket_style_resolve(&rt,200,0,NULL,&out)==POCKET_STYLE_TOKEN_MISSING);
 CHECK(pocket_style_add_rule(&rt,&(PocketStyleRule){300,1U<<31,1,{0}})==POCKET_STYLE_INVALID_ARGUMENT);
 PocketStyle invalidOpacity=atom_style(POCKET_STYLE_OPACITY,pocket_style_literal(257));
 CHECK(pocket_style_add_rule(&rt,&(PocketStyleRule){301,0,1,invalidOpacity})==POCKET_STYLE_INVALID_ARGUMENT);
 PocketThemeToken badToken[]={{77,300}};PocketThemeDefinition badTheme={3,(PocketStyle){0},badToken,1};
 CHECK(pocket_style_add_theme(&rt,&badTheme)==POCKET_STYLE_OK);
 PocketStyle tokenOpacity=atom_style(POCKET_STYLE_OPACITY,pocket_style_token(77));
 CHECK(pocket_style_add_rule(&rt,&(PocketStyleRule){302,0,1,tokenOpacity})==POCKET_STYLE_OK);
 CHECK(pocket_style_set_theme(&rt,3)==POCKET_STYLE_OK);
 CHECK(pocket_style_resolve(&rt,302,0,NULL,&out)==POCKET_STYLE_INVALID_ARGUMENT);
 pocket_style_runtime_dispose(&rt);puts("STYLE_OK");return 0;
}
