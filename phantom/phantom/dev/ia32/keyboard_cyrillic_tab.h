
// Gives out UTF-32 cyrillic char for ASCII incomin char

static const u_int32_t ascii_to_UTF_cyrillic [256] = {
    /* 00 */	0,	0,	0,	0,
    /* 04 */	0,	0,	0,	0,
    /* 08 */	0,	0,	0,	0,
    /* 0C */	0,	0,	0,	0,
    /* 10 */	0,	0,	0,	0,
    /* 14 */	0,	0,	0,	0,
    /* 18 */	0,	0,	0,	0,
    /* 1C */	0,	0,	0,	0,
    /* 20 */	0,	0,	0,	0,
    /* 24 */	0,	0,	0,	0,
    /* 28 */	0,	0,	0,	0,
    /* 2C */	0,	0,	0,	0,
    /* 30 */	0,	0,	0,	0,
    /* 34 */	0,	0,	0,	0,
    /* 38 */	0,	0,	0,	0,
    /* 3C */	/* < -> б */0x411,	0,	/* > -> ю */0x42E,	0,
    /* 40 */	0,	/* A -> ф */0x424,	/* B -> и */0x418,	/* C -> с */0x421,
    /* 44 */	/* D -> в */0x412,	/* E -> у */0x423,	/* F -> a */0x410,	/* G -> п */0x41F,
    /* 48 */	/* H -> р */0x420,	/* I -> ш */0x428,	/* J -> о */0x41E,	/* K -> л */0x41B,
    /* 4C */	/* L -> д */0x414,	/* M -> ь */0x42C,	/* N -> т */0x422,	/* O -> щ */0x429,
    /* 50 */	/* P -> з */0x417,	/* Q -> й */0x419,	/* R -> к */0x41A,	/* S -> ы */0x42B,
    /* 54 */	/* T -> е */0x415,	/* U -> г */0x413,	/* V -> м */0x41C,	/* W -> ц */0x426,
    /* 58 */	/* X -> ч */0x427,	/* Y -> н */0x41D,	/* Z -> я */0x42F,	/* [ -> х */0x425,
    /* 5C */	0,	/* ] -> ъ */0x42A,	0,	0,
    /* 60 */	/* ` -> ё */0x401,	0,	0,	0,
    /* 64 */	0,	0,	0,	0,
    /* 68 */	0,	0,	0,	0,
    /* 6C */	0,	0,	0,	0,
    /* 70 */	0,	0,	0,	0,
    /* 74 */	0,	0,	0,	0,
    /* 78 */	0,	0,	0,	0,
    /* 7C */	0,	0,	0,	0,
    /* 80 */	0,	0,	0,	0,
};

// TODO tolower: 0x410...42F -> 0x430...44F, 0x0401 -> 0x0451

// todo wchar_t cyrillic_translate( wchar_t key, int tolower )
// if returns 0 use basic translation

wchar_t ascii_to_UTF32_cyrillic_translate( wchar_t key, int tolower )
{
    if( key >= 256 ) return key;

    wchar_t n = ascii_to_UTF_cyrillic[ (unsigned char)key ];

    LOG_FLOW( 4, "cyr %x -> %x", key, n );

    if( !n ) return key;

    key = n;

    if( !tolower ) return key;

    if( key == 0x0401 ) return 0x0451;

    if( (key >= 0x410) && (key <= 0x42F) )
        return key + 0x20;

    return key;
}
