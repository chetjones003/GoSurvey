# GoSurvey one-command installer (REQ-454, ADR-072, ADR-073).
# Usage: irm https://raw.githubusercontent.com/chetjones003/GoSurvey/master/scripts/install.ps1 | iex
#
# This file is plain ASCII. Box and block characters are built at run time from code points so the
# script survives any download encoding.

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try {
    [Net.ServicePointManager]::SecurityProtocol =
        [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
} catch {}

# ---------------------------------------------------------------------------------------------
# Console presentation
# ---------------------------------------------------------------------------------------------

function New-InstallerUi {
    $interactive = $false
    try { $interactive = -not [Console]::IsOutputRedirected } catch {}
    $vt = $false
    if ($interactive -and [string]::IsNullOrEmpty($env:NO_COLOR)) {
        try { $vt = [bool]$Host.UI.SupportsVirtualTerminal } catch {}
        if ($env:WT_SESSION) { $vt = $true }
    }
    $width = 80
    try { $width = [int]$Host.UI.RawUI.WindowSize.Width } catch {}
    if ($width -lt 40) { $width = 80 }

    # Windows Terminal falls back to fonts that carry check marks and braille; classic consoles
    # (Consolas, Lucida Console) do not, so they get glyphs those fonts are known to contain.
    $modern = [bool]$env:WT_SESSION
    $glyphs = @{
        Ok      = if ($modern) { [string][char]0x2713 } else { [string][char]0x221A }
        Fail    = [string][char]0x00D7
        Note    = '!'
        Bar     = [string][char]0x2501
        Rule    = [string][char]0x2500
        Dot     = [string][char]0x00B7
        Spinner = if ($modern) {
            @(0x280B, 0x2819, 0x2839, 0x2838, 0x283C, 0x2834, 0x2826, 0x2827, 0x2807, 0x280F) |
                ForEach-Object { [string][char]$_ }
        } else { @('|', '/', '-', '\') }
    }
    if (-not $vt) {
        $glyphs.Ok = '+'; $glyphs.Fail = 'x'; $glyphs.Bar = '#'; $glyphs.Rule = '-'; $glyphs.Dot = '.'
    }

    return @{
        Vt     = $vt
        Live   = $vt -and $interactive
        Width  = $width
        Glyphs = $glyphs
        Esc    = [string][char]27
        Colors = @{
            Text   = @('230;236;244', 'White')
            Muted  = @('148;165;190', 'Gray')
            Dim    = @('88;104;128', 'DarkGray')
            Track  = @('56;72;98', 'DarkGray')
            Accent = @('110;170;235', 'Cyan')
            Amber  = @('232;168;72', 'Yellow')
            Ok     = @('94;214;140', 'Green')
            Fail   = @('244;114;114', 'Red')
        }
    }
}

# Writes styled text. Arguments alternate: style name, text, style name, text...
# -Redraw replaces the current console line (used for live step updates); it is ignored when the
# console cannot redraw in place.
function Write-Styled {
    param([object[]]$Parts, [switch]$NoNewline, [switch]$Redraw)
    $ui = $script:Ui
    if ($ui.Vt) {
        $line = New-Object System.Text.StringBuilder
        if ($Redraw -and $ui.Live) { [void]$line.Append("`r").Append($ui.Esc).Append('[K') }
        for ($i = 0; $i -lt $Parts.Count; $i += 2) {
            [void]$line.Append($ui.Esc).Append('[38;2;').Append($ui.Colors[$Parts[$i]][0]).Append('m')
            [void]$line.Append([string]$Parts[$i + 1])
        }
        [void]$line.Append($ui.Esc).Append('[0m')
        Write-Host $line.ToString() -NoNewline:$NoNewline
        return
    }
    for ($i = 0; $i -lt $Parts.Count; $i += 2) {
        Write-Host ([string]$Parts[$i + 1]) -ForegroundColor $ui.Colors[$Parts[$i]][1] -NoNewline
    }
    if (-not $NoNewline) { Write-Host '' }
}

function Write-GoSurveyBanner {
    $ui = $script:Ui
    $cardWidth = 100
    Write-Host ''
    if (-not $ui.Vt -or $ui.Width -lt ($cardWidth + 2)) {
        Write-Styled @('Text', '  GoSurvey', 'Dim', '  /  ', 'Accent', 'CAD for land surveyors')
        Write-Styled @('Amber', '  INSTALLER', 'Dim', '  /  ', 'Muted', "Latest stable $($ui.Glyphs.Dot) SHA-256 verified $($ui.Glyphs.Dot) Per-user")
        Write-Host ''
        return
    }

    $palette = @(
        '85;144;204', '88;148;208', '244;247;252', '0;12;29', '0;12;31', '0;19;43', '1;19;44',
        '0;19;44', '82;140;200', '79;137;196', '0;14;33', '0;14;35', '0;17;39', '0;21;49',
        '0;18;44', '77;133;192', '74;129;188', '255;255;255', '0;11;27', '0;16;39', '0;15;38',
        '0;14;37', '71;126;183', '69;122;180', '0;9;23', '0;11;28', '0;13;32', '0;14;34',
        '0;16;40', '0;20;48', '3;34;70', '12;49;84', '66;118;176', '63;115;172', '52;58;70',
        '60;111;168', '58;107;164', '55;104;160', '52;100;156', '126;172;222', '50;96;152', '47;93;148',
        '232;168;72', '44;89;144', '0;5;16', '42;86;140', '0;8;22', '0;0;0', '9;16;32',
        '150;170;196', '84;104;132', '172;193;201', '80;134;175', '54;96;138', '6;93;152', '37;67;100',
        '11;65;120', '0;70;137', '0;57;126', '23;43;66', '4;41;82', '0;51;125', '0;50;114',
        '0;45;121', '0;45;112', '0;42;115', '0;39;115', '0;35;78', '9;29;53', '7;21;41',
        '2;28;59', '2;20;45', '0;31;72', '0;28;68', '0;25;61', '0;21;56', '0;20;46',
        '3;16;35', '2;12;29', '0;15;36', '0;11;30', '0;10;25', '0;7;20', '0;6;17',
        '0;5;15', '0;4;13'
    )
    $rows = @'
 {{ {{ {{ {{ {{ {{ {{ __ __ __ __ __ __ uu __ __ YY ]] ]] ]] SS SS ZZ DD [[ [[ [[ KK ?? ?? ?? ?? ?? ?? ?? ?? ** ** ** << << << << << << << << << NN << ;; ;; ;; ;; ;; )) )) )) )) )) )) )) )) :: )) )) :: :: :: )) :: :: :: :: :: :: :: :: )) )) :: ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; dd << << << << **
 ss {{ {{ {{ {{ {{ {{ {{ {{ {{ __ __ __ __ __ __ __ __ YY ]] ]] ]] ]] SS ZZ [[ DD [[ [[ [[ [[ KK LL ?? ?? ?? MM MM ?? ** ** ** FF ** FF ** FF HH << << << << << << << ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; )) ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; << << dd << << dd << << << << ** cc cc TT ??
 || ss || || {{,A{^BA^BA^BA^BA^BA^BA^BA^BA^BA^BA^BA^BA^BA^BA^BA^BA,A] ]] ]] ]] SS,CS CC^CD^CD^CE CC,C[ KK ?? ?? ?? ?? ?? TT ?? ** **,C* CC^CF^CG^CH CC,C< << << << << << << << NN << ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; ;; << << << << << NN NN << << << OO OO OO ** ** ** ** cc UU ?? ?? ?? bb
 || || || || ||^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ^IJ YY YY ]] ]] CC CC SS ZZ ZZ DD [[ [[ [[,Ca CC^CK^CL CC,C? ?? TT^CM CC,C*,C*,C*,C* ** ** ** CC CC ** ** CC CC << << CC CC,Cd CC^CN^CN NN NN CC CC << << << CC CC << HH,CH CC^CO^CO CC,CO ** ?? CC CC ?? cc CC CC ?? ?? == ?? [[ [[ [[ [[
 || || || || ||^PQ^PQ^PQ^PR^PR^PR^PR^PR^PQ^PQ^PR^PR^PR^PR^PR^PQ^PQ^PQ uu __ YY ]] CC CC ]] ]]^CS CC CC ZZ DD CC CC [[ [[ CC CC ?? bb LL ?? ?? ?? UU CC CC ?? ?? CC CC ?? ?? CC CC ** HH CC CC^CO OO OO << << << OO CC CC ** CC CC ** ** cc CC CC^CT^CU^CU^CV ** ** CC CC ?? ?? CC CC:3&-37+17%17*17=z7+z&=z(
 || || || }} }}^WX^WX RR RR^WX^WX^WX^WX^WX RR RR^WX^WX^WX^WX^WX^WX^WX __ __ uu uu^CY CC,C],C],C] CC CC SS ZZ^CZ CC,C[,CE CC^Ca ?? ??^Cb CC,CL,C?,C? CC^CU ?? cc^Cc CC,C?,C* CC CC OO << CC CC << << << << << << NN dd CC CC CC << NN dd <<^Cd CC,C<,C;,C)^Ce-48*18^Cf CC,C),C) CC CC.8. )) ;; )):37:07:z7-z7
 {{ {{ {{ {{ {{^gh^gh RR RR^gh^Rh RR RR^gh^gh^Rh^Rh^Rh RR^gR^gh^gh^gh ]] ]] ]] ]] ]] ]] ]] ]] ]] SS [[ [[ [[ EE aa ?? KK bb LL LL LL bb ?? ?? ?? ?? ?? TT UU ?? UU ?? ?? ** ** ** << << <<.%N.8< << << << << << << dd << << <<.8;.8).+)-4)-3)-4.-4-:4-:+%.!- :: )).3-,C7,C7,C3 CC^Ci=17+17=17+17:37.1).1&.1&
 {{ {{ {{ {{ {{^jk^jk RR RR^jk^jk RR RR^jk^jk^jk^jk^jk RR RR^jk^jk^jk YY ]] ]] ]] ]] ]] ]] ZZ ZZ [[ [[ [[ [[ EE.-[ EE EE ??.%b LL ??.%?.%? ?? cc ??.%?.%*.8*.8<.8<.8< << << << OO ** cc cc ??.8* <<.8< NN.8;.8;.8;:+):3)-3)=3&-3&:3):3).8; ;; ;; ;; )):4):3):3):3&:3&.3& &&.3&:37 &&.37.37:37:17-17=z7-z7-z7
 || || || || ss^lm^lm^lm^Rm^Rm^Rm^Rm^Rm^lm^Rm^Rm^Rm^Rm^Rm^lm^lm^lm^lm uu YY ]] ]]Cn] ]]An] SSDn] [[ [[ [[Fn[ [[On[ aaRnE [[ aa aaLn? ??An? ??Nn? ??Dn= (( && **Sn& ))Un) ))Rn) ))Vn) ))En. ..Yn. ..On- --Rn- ..Sn).8).+):+).8):4)-4.-4.:4--4--48-4%-4% )) ;; ;;.8;.8; << <<.8< <<.8;:3&.3&.3&.3&.8&.1&.1&.1&
 || || || || ||^op^op^op^op^op^op^op^op^op^op^op^op^op^op^op^op^op^op uu YY ]] ]]~q]~q]~q]~q]~qS~qZ~q[~q[~q[~qa~qa~qa [[.%=:7=-3=:3=:3==3(:3(-3(:3(-3(-3(*1&:3(.3(:3(.8( ** &&.3).3):37+17+18:48-48-48:48.+% -- :: ;; ;; ;; ;; ;; << ;; )).!).!):+):+):4.:+--4-.!..8).8).8) ;; << <<.8; ;;.3):3-:48.2-.2).2)
 || || || || ||^rs^rt^rt^rt^rt^rt^rt^rt^rt^rt^rt^rt^rt^rt^rt^rt^rt^ru uu YY YY ]] YY ]] ]] ]] SS ZZ SS ZZ.%>=3=-3=.8>.->.-> >> == ==.%=.7=.7=.3= ** ** ??:3* HH ** ** ((*1&+17+17-38-38:38=18:38:38:38.8. ee --.!-.!-.+-:+-.+-.8. :: )).8) << << << << OO ** **.8):4)-4.:4-:4-.!..8..8..3).8) )).8).2).2;.2;
 || || || || || ss ss ss ss ss {{ {{ {{ {{ {{ {{ {{ {{ {{ __ __ __ __ __ __ uu YY ]] ]] ]] ]] ]] ]] ]] ]] ]] ZZ >>:3>:3>:3> ??.%? ?? ??.%?.8=:8(:8*:3(+1&%07=1&:3&:3&-3).8).8).!..+-.+8.48.48.48:48:43-13#07=17.37.8-.3).3..8. -- --.8- .. :: )) )).3) <<-3*.8*.3* ** cc ** &&:3).3).+):+.:4-.+-.8).5).2..5.
 || || || || || || || || || || || || ss {{ {{ {{ {{ {{ {{ {{ {{ __ __ __ __ __ uu uu YY ]] ]] ]] ]] ]] ]] ZZ ZZ ZZ [[ [[.8=:3=:3(+3(-3=-3=-3=-3(=3=:3=:3(-3(.8(=3(.3*:3&=3&+17:37.8).8..+-.+%.+8.+8 88.48:48:48:13-13-17:17-17=17-17:3&+17:3&-3&-3& ;; **-3& (( TT TT:3&+1&*4(=1(+4(@03*07 <<.8).8):2).2):2.
 }} }} || || || || || || || || || || || || || {{ {{ {{ {{ {{ {{ {{ __ __ __ __ __ __ uu uu uu YY ]] ZZ [[.5a.5?.2?.2?.5? ??.5?.5K.2=-0=:2>.5> DD [[ aa [[ bb-2=-0(:0(:2==z(#z7*z7=z& )).2).5).2-.2-.2%.28.28.28.28.28.28.28.28.13:13:03-03:07-07*z7=z&=z7%z7-z&.5( <<*z7#z7-0&#z7*z&*z3@z1:2& cc == ?? == ((
 }} }} }} }} || || }} || || || || || || || || || ss {{ {{ {{ {{ {{ {{ __ __ __ __ uu YY YY.5].2[:2[.2K:2K.2> [[ [[ [[ [[ ]] ]] ]] [[ [[ >>.2=+z(%z&:0=:0= == == ?? ?? ?? ?? ?? ?? ===z(#z7 )) )) )) .. ...1- --.2%.2%.2%.58.28.28.28 88 88.28.28.13:13:08-07-07-07:2-:2< KK+0) ** ))=z3@z0%z3 ** **.2< <<.5<
 }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} || {{ {{ {{ __ __ __ __.5u YY.5Y.2].5].5].5Z.5Z ]] ]] ]] ]] ]] ]] ]] [[ [[ [[ ZZ ]] ZZ [[ [[ >>=0= [[ [[ [[ ?? LL ?? ?? ** ** (( ** ==*z& &&.1) )).1).4) )).5).2..5-.5-.2-.5- -- --.2%.28.28.28.28.28.28.28.28:28 ;; ==:2D YY ]] FF=z7@z0%R3 ((.5*.2):2.-0)
 }} }} }} }} }} }} }} }} }} }} }} || || || || || ss {{ {{ {{ __ __ __ __ __ ]] ]] YY uu __ ]] uu ]] YY ]] ]].5] ]] [[.5[ ]] ZZ [[ [[ [[ [[ ]] ===z(.5=-z==z= == == ?? == == ?? MM MM TT+z& && ** **.1< << <<.2; ;;.2;.2).2).2).2).2).2..2..2..2).5. ...5- --.5%.2-.5).5? ==*z(=z= ;;=z7@z1%z3 )).5)=z7-0).2*
 || || || || || || || || || }} || || || || || || || || ss ss {{ {{ {{ __ __ __ __ __ __ __ __ uu ]] ]] ]] ]] ]] ]] ]] ]] ]] ZZ [[ [[ ZZ [[ >>=z(@z7+z=:2=:0(-0=-2= == KK bb LL ?? ?? ??#z&.5& ** ** **.1*.1*:0*.2*.2<.2<.2;.2;.2<.5<.5; ;; ;; )) ))=0):0&-0)-0).1).5;.5L.5?.5(.5&:2.-07+z3@z3-27.2* ==.2? MM
 }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} || ss ss {{ {{ {{ {{ __ __ __ __ __ __ __ __ uu ]] ]] ]] ]] ]] ]] ]] ]] ZZ.5[ [[ [[ [[ [[ [[ >> EE >>.1>:2=:0=:0=-z=-2= == ?? bb aa+z( ** MM MM ** ** ** **.0( ** <<.5; ;;.5;.5&+z&:0&-0&-0)-0) )):0)-0-=z)-z&=z&.2].2].5_.5>-0&+z7*z7=0& ?? LL LL:2*.1(
 }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} || ss || ss ss {{ ss {{ {{ {{ __ __ __ __ uu YY YY ]] ]] ]] ]] ]] SS ZZ.5D DD.5[ ZZ >>.5>.1> >> >> >> EE aa aa ?? ==.5=.0=.2=-z=-2=.5=+z& (( ?? ??.1T ?? ?? ?? ** **:0*-0&-0&-0&=z&*z&.5*:2&:0&=z&=z&=z&:2* ** ==*z= }} }} {{.5]:1&#z7@z7 == bb.2=.6?:1=.6=
 }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} || ss.5{ ss {{ {{ {{ {{ {{.5_.5_.5_.5] YY ]] YY YY ]] ]] ]] ]] ]] SS.2].1] >> >> >>.1>.1> >> >> [[ [[ [[ [[ >> >> == ?? ?? ==.5=.5=.0=-z(%z7*z&=z(:2(:1( ((-2(-0(-0(-0(:0(:1(-0(-0(-0*=z*-0&-0(:2( ((-2(-z&+z7#z7 >> {{*z(=z> }}:2<:28*z7=z7@z7 == [[ [[ ?? ??
 }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} }} || ss {{ {{ {{ __ __ __ __ __ __ YY.5Y.5].5] ]].5].5u.4_.2_ ]] uu YY ]] ]] ]] ]] ]] >>:0>:0>:1> >> >> >> >> >> >> >> ==.1= == == >>-0= >> >>-z(%z7=z(.1(:0&-0&-0&=z&-z&=0&+z& && ((.2?:0(-z(:0&=z7:z7-z7+03+07-z7+z7=z& }}:2(+z7%z3#z7#z7%z7 (( == == [[
.6} }} }} }} }} }} }} }} }} }} }} }} }} }} || }} }} ss.5{ || ss {{ __.2u __ __ __ {{ __.6_ __.5] ]] >>.5>.5>:2=:2?:2*=0*+0&=0&-0&=0(.5= == >> ]] ]] ]].6[.5[.5[ >> >> >> >> EE EE [[ [[-2?.2?.5M **:0(-0*:0*-0&=z&:1&-0)-0)-2-:2%+0-=0):1)=07+z7:07.1.:18+08+07.07.07-z7-z( || >>=03*z3@R1 ((#z7%z3*z7+z7=z7
 }} }} }} }} }} }} }} }} }} }} }}.5| || || || || {{ {{ {{ {{.5_ __ __ __ __ __ ]].6].2>.1>-0(-0(-0(=0(-0=:2*=0*:2<.5? ?? LL.2<:2):2)-0)-2)=0)-0&=0&-0&=0(:2( ==.2>.2[.2[.2[:2?.5?.6=:0=-0(-0(:0(-0(:0(.1(.1( ** << dd dd ;;:2).2.-0--0-:0):1).5& **:27+07+07.2( GG.1*.5&:0= }} }}-07+z3@z1+z7.0>%z7+z7 77.07
 }} }} }} }} }} }}.5} }} }} || || ||.5| ||.5| {{ {{.5_.5_.2_ __:0]-0>=0=-0(-0(-0*-2*=0(-2(:2?.5=:1>.5K:2b.5?:1?.5[.5[.2L.5c.5c **.5* GG.5<.5<.5<.5*-0&:1&-0&=0&-0&:2&:2<-0&=0&+0&=0(:1(.2=:2?:2G.2*.5<.2<.5;:2):2):2.:2.:2--2--0).5) << ??.2c [[-0&*07+07 ==:1*:2*:1*-0&:2= ||.1{:2(+03*z3@z1 (( __%z7*z7 **
.5}.5}.5}.5} }}.5|.5|.5|.5| || || ss.2{.5{.5_.5_ __.1_:0]:0]:0>:1>.5>.5D.5D.2D ]] ]].5].5[-0>.5].5] ]] ]] ]] ]] ]] [[ [[ ??:2?-0*.5?.5[.5[ EE ?? ??.5? [[ [[ ?? ??.5*.2*.5*.5*.2<.2< <<.5* ** ** cc.2* OO OO.5;.2;.2N.2<.5* ??.5*:2*.2<-0&:2*:2= >>:1>.5> [[ ??.5? ?? [[ >> {{ ]]-0=-0&=07#z7*z7 __ {{@z7.17
 }}.5}.5| ||.2{.2{.2{.2{.2{.2_:2_:2].2].2]:2]:2]:0]:2_ ]] __ uu.5u __ __ YY __ uu.5] ]] ]] >> ]] ]] __ ]] ]] ]] ]].5Z.5] ]] ]] __ ]] ]] ]] [[ [[.5?:0( ?? ?? LL [[ ?? LL ?? ??.5c.5?.5T.5T.5U VV ?? ?? ?? ?? [[.5? [[ [[ ?? VV UU aa.9a.5(=07+07:1( ]] ?? ?? [[ [[.2? ?? ]] || __:0> __=0(+z7%z7 == uu:0(@z7
 ww ww wwIqwNqwSqwTqwAqwLqwLqwEqwRqw ww ww ww wwLxwaxwtxwexwsxwtxw wwsxwtxwaxwbxwlxwexw ww ww`xw ww wwSxwHxwAxw-xw2xw5xw6xw wwvxwexwrxwixwfxwixwexwdxw ww ww`xw ww wwPxwexwrxw-xwuxwsxwexwrxw ww ww ww ww ww ww ww ww ww ww ww ww ww ww ww ww ww ww wwWywiywnywdywoywwywsyw ww1yw0yw ww/yw ww1yw1yw ww ww ww
'@ -split "`r?`n"
    $glyphMap = @{
        '^' = [string][char]0x2580; ',' = [string][char]0x2584
        '~' = [string][char]0x2500; '`' = [string][char]0x00B7
    }
    $indexChars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/!#$%&()*-.:;<=>?[]_{|}'
    $margin = '  '
    $esc = $ui.Esc
    foreach ($row in $rows) {
        if ([string]::IsNullOrEmpty($row)) { continue }
        $line = New-Object System.Text.StringBuilder
        [void]$line.Append($margin)
        $lastFg = -1; $lastBg = -1
        for ($i = 0; $i -lt $row.Length; $i += 3) {
            $glyph = [string]$row[$i]
            $fg = $indexChars.IndexOf($row[$i + 1])
            $bg = $indexChars.IndexOf($row[$i + 2])
            if ($bg -ne $lastBg) { [void]$line.Append($esc).Append('[48;2;').Append($palette[$bg]).Append('m'); $lastBg = $bg }
            if ($glyph -ne ' ' -and $fg -ne $lastFg) { [void]$line.Append($esc).Append('[38;2;').Append($palette[$fg]).Append('m'); $lastFg = $fg }
            if ($glyphMap.ContainsKey($glyph)) { $glyph = $glyphMap[$glyph] }
            [void]$line.Append($glyph)
        }
        [void]$line.Append($esc).Append('[0m')
        Write-Host $line.ToString()
    }
    Write-Host ''
}

$script:StepLabelWidth = 10

function Write-Step {
    param(
        [ValidateSet('Run', 'Ok', 'Fail', 'Note')][string]$State,
        [string]$Label,
        [object[]]$Detail = @(),
        [int]$Frame = 0,
        [switch]$InProgress
    )
    $g = $script:Ui.Glyphs
    switch ($State) {
        'Run'  { $icon = @('Amber', $g.Spinner[$Frame % $g.Spinner.Count]) }
        'Ok'   { $icon = @('Ok', $g.Ok) }
        'Fail' { $icon = @('Fail', $g.Fail) }
        'Note' { $icon = @('Amber', $g.Note) }
    }
    $labelStyle = if ($State -eq 'Fail') { 'Fail' } elseif ($State -eq 'Note') { 'Amber' } else { 'Text' }
    $parts = @('Dim', '  ') + $icon + @('Dim', '  ', $labelStyle, $Label.PadRight($script:StepLabelWidth)) + $Detail
    Write-Styled $parts -Redraw -NoNewline:$InProgress
}

# Draws the live line for the active step; a no-op when the console cannot redraw in place.
function Write-StepProgress {
    param([string]$Label, [object[]]$Detail = @())
    if (-not $script:Ui.Live) { return }
    $script:SpinnerFrame++
    Write-Step -State Run -Label $Label -Detail $Detail -Frame $script:SpinnerFrame -InProgress
}

function Format-Size {
    param([long]$Bytes)
    if ($Bytes -ge 1MB) { return ('{0:N1} MB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N0} KB' -f ($Bytes / 1KB)) }
    return "$Bytes B"
}

function Get-ProgressBar {
    param([double]$Fraction, [int]$Width = 30)
    $filled = [int][Math]::Round([Math]::Max(0, [Math]::Min(1, $Fraction)) * $Width)
    $bar = $script:Ui.Glyphs.Bar
    return @('Accent', ($bar * $filled), 'Track', ($bar * ($Width - $filled)))
}

function Wait-InstallerProcess {
    param([System.Diagnostics.Process]$Process, [string]$Label, [object[]]$Detail)
    while (-not $Process.WaitForExit(90)) {
        Write-StepProgress -Label $Label -Detail $Detail
    }
    $Process.WaitForExit()
}

function Save-InstallerDownload {
    param([Uri]$Uri, [string]$Path, [string]$Label)
    $request = [Net.HttpWebRequest]::Create($Uri)
    $request.UserAgent = 'GoSurveyInstaller'
    $response = $request.GetResponse()
    try {
        $total = [long]$response.ContentLength
        $source = $response.GetResponseStream()
        $target = [IO.File]::Create($Path)
        try {
            $buffer = New-Object byte[] 262144
            $received = 0L
            $lastDraw = [DateTime]::MinValue
            while (($read = $source.Read($buffer, 0, $buffer.Length)) -gt 0) {
                $target.Write($buffer, 0, $read)
                $received += $read
                if ($script:Ui.Live -and ([DateTime]::UtcNow - $lastDraw).TotalMilliseconds -ge 70) {
                    $lastDraw = [DateTime]::UtcNow
                    if ($total -gt 0) {
                        $fraction = $received / $total
                        $detail = (Get-ProgressBar $fraction) + @(
                            'Text', ('  {0,3:N0}%' -f ($fraction * 100)),
                            'Dim', ('   {0} / {1}' -f (Format-Size $received), (Format-Size $total)))
                    } else {
                        $detail = @('Muted', (Format-Size $received))
                    }
                    Write-StepProgress -Label $Label -Detail $detail
                }
            }
        }
        finally {
            $target.Dispose()
            $source.Dispose()
        }
    }
    finally {
        $response.Close()
    }
    return $received
}

function Write-InstallSummary {
    param([string]$Version, [string]$Location)
    $g = $script:Ui.Glyphs
    $arrow = if ($script:Ui.Vt) { [string][char]0x2192 } else { '>' }
    Write-Host ''
    Write-Styled @('Track', ('  ' + ($g.Rule * [Math]::Min(100, $script:Ui.Width - 4))))
    Write-Host ''
    Write-Styled @('Ok', "  $($g.Ok)  ", 'Text', "GoSurvey $Version is ready.")
    Write-Host ''
    Write-Styled @('Dim', '     Location   ', 'Muted', $Location)
    Write-Styled @('Dim', '     Launch     ', 'Muted', "Start menu  $arrow  GoSurvey")
    Write-Host ''
}

# ---------------------------------------------------------------------------------------------
# Installation helpers
# ---------------------------------------------------------------------------------------------

function Get-MachineInstall {
    $appIdKey = '{02774040-27C9-4A9A-A999-86E641C42F78}_is1'
    $uninstallRoots = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'
    )

    foreach ($root in $uninstallRoots) {
        $entry = Get-ItemProperty -LiteralPath (Join-Path $root $appIdKey) -ErrorAction SilentlyContinue
        # Older installers included the version in DisplayName (for example,
        # "GoSurvey version 0.7.4"); newer per-user entries use their own label.
        # The stable AppId key identifies this product across both forms.
        if ($null -eq $entry -or $entry.DisplayName -notmatch '^GoSurvey(?: version .+)?$') { continue }

        $installLocation = [string]$entry.InstallLocation
        if ([string]::IsNullOrWhiteSpace($installLocation)) {
            $installLocation = Join-Path $env:ProgramFiles 'GoSurvey'
        }

        $programFilesRoots = @($env:ProgramFiles, ${env:ProgramFiles(x86)}) |
            Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        $isProgramFilesInstall = $false
        foreach ($programFilesRoot in $programFilesRoots) {
            $rootWithSeparator = $programFilesRoot.TrimEnd('\') + '\'
            if ($installLocation.StartsWith($rootWithSeparator, [StringComparison]::OrdinalIgnoreCase)) {
                $isProgramFilesInstall = $true
                break
            }
        }

        if ($isProgramFilesInstall) {
            $uninstaller = Join-Path $installLocation 'unins000.exe'
            if (-not (Test-Path -LiteralPath $uninstaller -PathType Leaf)) {
                throw "Found a machine-wide GoSurvey install at '$installLocation', but its uninstaller is missing. No changes were made."
            }
            return [pscustomobject]@{ InstallLocation = $installLocation; Uninstaller = $uninstaller }
        }
    }

    return $null
}

function Get-PerUserInstallVersion {
    $appIdKey = '{02774040-27C9-4A9A-A999-86E641C42F78}_is1'
    $entry = Get-ItemProperty -LiteralPath (Join-Path 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall' $appIdKey) -ErrorAction SilentlyContinue
    if ($null -eq $entry -or $entry.DisplayName -notmatch '^GoSurvey(?: version .+)?$') { return $null }
    return [string]$entry.DisplayVersion
}

function Test-InstalledVersionAtLeast {
    param([string]$InstalledVersion, [string]$AvailableVersion)

    $installed = $null
    $available = $null
    if (-not [version]::TryParse($InstalledVersion, [ref]$installed) -or
        -not [version]::TryParse($AvailableVersion, [ref]$available)) {
        return $false
    }
    return $installed -ge $available
}

function Copy-UserDataBack {
    param([string]$Source, [string]$Backup)

    if (-not (Test-Path -LiteralPath $Backup -PathType Container)) { return }
    if (Test-Path -LiteralPath $Source) {
        Remove-Item -LiteralPath $Source -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Source -Force | Out-Null
    Get-ChildItem -LiteralPath $Backup -Force | Copy-Item -Destination $Source -Recurse -Force
}

function ConvertFrom-ReleaseManifestContent {
    param([object]$Content)

    if ($Content -is [byte[]]) {
        $Content = [Text.Encoding]::UTF8.GetString($Content)
    }
    return (ConvertFrom-Json -InputObject $Content)
}

# ---------------------------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------------------------

$script:Ui = New-InstallerUi
$script:SpinnerFrame = 0
Write-GoSurveyBanner

$tempRoot = Join-Path $env:TEMP ('GoSurvey-Install-' + [Guid]::NewGuid().ToString('N'))
$manifestUrl = 'https://github.com/chetjones003/GoSurvey/releases/latest/download/latest.json'
$dataPath = Join-Path $env:APPDATA 'GoSurvey'
$dataBackupPath = Join-Path $tempRoot 'user-data'
$installerPath = Join-Path $tempRoot 'GoSurvey-Installer.exe'
$targetPath = Join-Path $env:LOCALAPPDATA 'Programs\GoSurvey'
$dataWasBackedUp = $false
$migrationMayHaveChangedData = $false
$preserveRecoveryFiles = $false
$dataRestoreFailed = $false
$installerSucceeded = $false
$installFailed = $false
$currentStep = 'Release'
$dot = $script:Ui.Glyphs.Dot

if ($script:Ui.Live) { Write-Host "$($script:Ui.Esc)[?25l" -NoNewline }
try {
    New-Item -ItemType Directory -Path $tempRoot -Force | Out-Null

    $currentStep = 'Release'
    Write-StepProgress -Label 'Release' -Detail @('Muted', 'Checking the latest stable release')
    $manifestResponse = Invoke-WebRequest -Uri $manifestUrl -Headers @{ 'User-Agent' = 'GoSurveyInstaller' } -UseBasicParsing
    if ($manifestResponse.StatusCode -ne 200) {
        throw "Stable release manifest request returned HTTP $($manifestResponse.StatusCode)."
    }
    $manifest = ConvertFrom-ReleaseManifestContent -Content $manifestResponse.Content
    if ([string]$manifest.channel -ne 'stable') {
        throw 'The release manifest is not marked as stable. Installation stopped.'
    }
    if ([string]::IsNullOrWhiteSpace([string]$manifest.version)) {
        throw 'The stable release manifest is missing its version. Installation stopped.'
    }
    if ([string]$manifest.sha256 -notmatch '^[0-9a-fA-F]{64}$') {
        throw 'The stable release manifest is missing a valid SHA-256 hash. Installation stopped.'
    }
    $installerUri = $null
    if (-not [Uri]::TryCreate([string]$manifest.installerUrl, [UriKind]::Absolute, [ref]$installerUri) -or
        $installerUri.Scheme -ne 'https' -or $installerUri.Host -ne 'github.com') {
        throw 'The stable release manifest is missing a valid HTTPS GitHub installerUrl. Installation stopped.'
    }
    $version = [string]$manifest.version
    Write-Step -State Ok -Label 'Release' -Detail @('Text', "GoSurvey $version", 'Dim', "  $dot  stable channel")

    # Keep a protected Program Files installation on the migration path even when its
    # version is current. Otherwise, do not download or reinstall a version already present.
    $machineInstall = Get-MachineInstall
    $perUserVersion = Get-PerUserInstallVersion
    if (-not $machineInstall -and (Test-InstalledVersionAtLeast -InstalledVersion $perUserVersion -AvailableVersion $version)) {
        Write-Step -State Ok -Label 'Install' -Detail @('Text', "GoSurvey $perUserVersion is already installed", 'Dim', '  no download needed')
        Write-InstallSummary -Version $perUserVersion -Location $targetPath
        return
    }

    $currentStep = 'Download'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $downloaded = Save-InstallerDownload -Uri $installerUri -Path $installerPath -Label 'Download'
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf) -or (Get-Item -LiteralPath $installerPath).Length -eq 0) {
        throw 'The installer download is empty or missing. Installation stopped.'
    }
    Write-Step -State Ok -Label 'Download' -Detail @(
        'Text', (Format-Size $downloaded),
        'Dim', ('  {0}  {1:N1} s' -f $dot, $clock.Elapsed.TotalSeconds))

    $currentStep = 'Verify'
    Write-StepProgress -Label 'Verify' -Detail @('Muted', 'Checking SHA-256')
    # .NET directly rather than Get-FileHash: no dependency on module auto-loading.
    $hashStream = [IO.File]::OpenRead($installerPath)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try { $actualHash = [BitConverter]::ToString($sha256.ComputeHash($hashStream)).Replace('-', '') }
    finally { $sha256.Dispose(); $hashStream.Dispose() }
    if (-not [string]::Equals($actualHash, [string]$manifest.sha256, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The downloaded installer SHA-256 does not match the stable release manifest. Installation stopped.'
    }
    Write-Step -State Ok -Label 'Verify' -Detail @(
        'Text', 'SHA-256 matches the release manifest',
        'Dim', ('  {0}  {1}' -f $dot, $actualHash.Substring(0, 12).ToLowerInvariant()))

    $desktopShortcuts = @(
        (Join-Path $env:PUBLIC 'Desktop\GoSurvey.lnk'),
        (Join-Path ([Environment]::GetFolderPath('Desktop')) 'GoSurvey.lnk')
    )
    $createDesktopShortcut = $false
    foreach ($shortcut in $desktopShortcuts) {
        if (Test-Path -LiteralPath $shortcut) { $createDesktopShortcut = $true; break }
    }

    if ($machineInstall) {
        $currentStep = 'Migrate'
        if (Test-Path -LiteralPath $dataPath -PathType Container) {
            New-Item -ItemType Directory -Path $dataBackupPath -Force | Out-Null
            Get-ChildItem -LiteralPath $dataPath -Force | Copy-Item -Destination $dataBackupPath -Recurse -Force
            $dataWasBackedUp = $true
        }

        Write-Step -State Note -Label 'Approval' -Detail @('Muted', 'Windows will ask once to remove the old Program Files copy.')
        $migrateDetail = @('Muted', 'Moving GoSurvey from Program Files to your user profile')
        Write-StepProgress -Label 'Migrate' -Detail $migrateDetail
        # The in-app updater launches this script and exits; an old GoSurvey.exe still holding its
        # files would make the uninstaller leave the Program Files copy behind.
        $legacyExecutable = Join-Path $machineInstall.InstallLocation 'GoSurvey.exe'
        $exitDeadline = [DateTime]::UtcNow.AddSeconds(30)
        while ([DateTime]::UtcNow -lt $exitDeadline) {
            $running = @(Get-Process -Name 'GoSurvey' -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -eq $legacyExecutable })
            if ($running.Count -eq 0) { break }
            Start-Sleep -Milliseconds 250
        }
        if (@(Get-Process -Name 'GoSurvey' -ErrorAction SilentlyContinue |
                Where-Object { $_.Path -eq $legacyExecutable }).Count -gt 0) {
            throw 'The old GoSurvey is still running. Close it and run the command again. The new installer was not run.'
        }
        try {
            $uninstall = Start-Process -FilePath $machineInstall.Uninstaller -ArgumentList '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' -Verb RunAs -PassThru
            $migrationMayHaveChangedData = $true
        }
        catch {
            throw 'Migration was cancelled or Windows could not start the old uninstaller. The new installer was not run.'
        }
        $null = $uninstall.Handle
        Wait-InstallerProcess -Process $uninstall -Label 'Migrate' -Detail $migrateDetail
        if ($uninstall.ExitCode -ne 0) {
            throw "The old GoSurvey uninstaller returned exit code $($uninstall.ExitCode). The new installer was not run."
        }
        # A successful process exit is not enough to guarantee the machine-wide copy is gone.
        # In particular, leaving its shared Start Menu shortcut makes Windows launch the old
        # Program Files executable even after the per-user installer creates its own shortcut.
        if ((Get-MachineInstall) -or (Test-Path -LiteralPath $legacyExecutable)) {
            throw 'The old GoSurvey installation is still registered after its uninstaller finished. The new installer was not run.'
        }
        $machineStartMenuShortcut = Join-Path $env:ProgramData 'Microsoft\Windows\Start Menu\Programs\GoSurvey.lnk'
        if (Test-Path -LiteralPath $machineStartMenuShortcut) {
            Remove-Item -LiteralPath $machineStartMenuShortcut -Force -ErrorAction Stop
        }
        Write-Step -State Ok -Label 'Migrate' -Detail @('Text', 'Removed the Program Files copy', 'Dim', "  $dot  settings kept")
    }

    $currentStep = 'Install'
    $installDetail = @('Muted', 'Installing for this Windows user')
    Write-StepProgress -Label 'Install' -Detail $installDetail
    $installArguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/SKIPVCREDIST=1', "/DIR=`"$targetPath`"")
    if ($env:GOSURVEY_INAPP_UPDATE -eq '1') { $installArguments += '/RELAUNCH=1' }
    if ($createDesktopShortcut) { $installArguments += '/TASKS=desktopicon' }
    $install = Start-Process -FilePath $installerPath -ArgumentList $installArguments -PassThru
    $null = $install.Handle
    Wait-InstallerProcess -Process $install -Label 'Install' -Detail $installDetail
    if ($install.ExitCode -ne 0) {
        throw "GoSurvey setup returned exit code $($install.ExitCode)."
    }
    # The Start Menu must now open the per-user copy. A surviving machine-wide registration means the
    # old version is still what launches, so report failure instead of a false "ready".
    if (Get-MachineInstall) {
        throw 'The old Program Files copy of GoSurvey is still installed, so the Start Menu may open the old version.'
    }
    $installerSucceeded = $true
    Write-Step -State Ok -Label 'Install' -Detail @('Text', 'Installed for this Windows user', 'Dim', "  $dot  no administrator needed")
}
catch {
    # A clean message instead of PowerShell's error dump; LASTEXITCODE tells scripted callers.
    $installFailed = $true
    Write-Step -State Fail -Label $currentStep -Detail @('Fail', ($_.Exception.Message -replace '\s*Installation stopped\.$', ''))
}
finally {
    if ($script:Ui.Live) { Write-Host "$($script:Ui.Esc)[?25h" -NoNewline }
    if ($dataWasBackedUp -and $migrationMayHaveChangedData) {
        try {
            Copy-UserDataBack -Source $dataPath -Backup $dataBackupPath
        }
        catch {
            Write-Step -State Fail -Label 'Restore' -Detail @('Fail', "GoSurvey's saved user data could not be restored from '$dataBackupPath': $($_.Exception.Message)")
            $preserveRecoveryFiles = $true
            $dataRestoreFailed = $true
        }
    }
    if ((Test-Path -LiteralPath $tempRoot) -and -not $preserveRecoveryFiles) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if ($dataRestoreFailed) {
    $installFailed = $true
    Write-Step -State Fail -Label 'Restore' -Detail @('Fail', "Your saved data backup remains at '$dataBackupPath'.")
}

if ($installFailed) {
    Write-Host ''
    Write-Styled @('Muted', '     Installation stopped. ', 'Dim', 'Run the command again, or download the installer from')
    Write-Styled @('Accent', '     https://github.com/chetjones003/GoSurvey/releases/latest')
    Write-Host ''
    $global:LASTEXITCODE = 1
    return
}

if ($installerSucceeded) {
    Write-InstallSummary -Version $version -Location $targetPath
}
