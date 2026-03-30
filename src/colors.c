#include "colors.h"
#include <string.h>

/* Helper macro: 8-bit RGB to GdkRGBA */
#define C(r,g,b) { (r)/255.0, (g)/255.0, (b)/255.0, 1.0 }

static const gchar *color_labels[BT_PALETTE_SIZE] = {
    "Black",   "Red",       "Green",   "Yellow",
    "Blue",    "Magenta",   "Cyan",    "White",
    "Bright Black", "Bright Red", "Bright Green", "Bright Yellow",
    "Bright Blue",  "Bright Magenta", "Bright Cyan", "Bright White",
};

static const BtColorScheme schemes[] = {
    {
        .name = "Tango (Default)",
        .fg = C(211, 215, 207),
        .bg = C(46, 52, 54),
        .palette = {
            C(46,52,54),    C(204,0,0),     C(78,154,6),   C(196,160,0),
            C(52,101,164),  C(117,80,123),  C(6,152,154),  C(211,215,207),
            C(85,87,83),    C(239,41,41),   C(138,226,52), C(252,233,79),
            C(114,159,207), C(173,127,168), C(52,226,226), C(238,238,236),
        },
    },
    {
        .name = "Solarized Dark",
        .fg = C(131, 148, 150),
        .bg = C(0, 43, 54),
        .palette = {
            C(7,54,66),     C(220,50,47),   C(133,153,0),  C(181,137,0),
            C(38,139,210),  C(211,54,130),  C(42,161,152), C(238,232,213),
            C(0,43,54),     C(203,75,22),   C(88,110,117), C(101,123,131),
            C(131,148,150), C(108,113,196), C(147,161,161),C(253,246,227),
        },
    },
    {
        .name = "Solarized Light",
        .fg = C(101, 123, 131),
        .bg = C(253, 246, 227),
        .palette = {
            C(7,54,66),     C(220,50,47),   C(133,153,0),  C(181,137,0),
            C(38,139,210),  C(211,54,130),  C(42,161,152), C(238,232,213),
            C(0,43,54),     C(203,75,22),   C(88,110,117), C(101,123,131),
            C(131,148,150), C(108,113,196), C(147,161,161),C(253,246,227),
        },
    },
    {
        .name = "Monokai",
        .fg = C(248, 248, 242),
        .bg = C(39, 40, 34),
        .palette = {
            C(39,40,34),    C(249,38,114),  C(166,226,46), C(244,191,117),
            C(102,217,239), C(174,129,255), C(161,239,228),C(248,248,242),
            C(117,113,94),  C(249,38,114),  C(166,226,46), C(244,191,117),
            C(102,217,239), C(174,129,255), C(161,239,228),C(249,248,245),
        },
    },
    {
        .name = "Gruvbox Dark",
        .fg = C(235, 219, 178),
        .bg = C(40, 40, 40),
        .palette = {
            C(40,40,40),    C(204,36,29),   C(152,151,26), C(215,153,33),
            C(69,133,136),  C(177,98,134),  C(104,157,106),C(168,153,132),
            C(146,131,116), C(251,73,52),   C(184,187,38), C(250,189,47),
            C(131,165,152), C(211,134,155), C(142,192,124),C(235,219,178),
        },
    },
    {
        .name = "Gruvbox Light",
        .fg = C(60, 56, 54),
        .bg = C(251, 241, 199),
        .palette = {
            C(40,40,40),    C(204,36,29),   C(152,151,26), C(215,153,33),
            C(69,133,136),  C(177,98,134),  C(104,157,106),C(168,153,132),
            C(146,131,116), C(251,73,52),   C(184,187,38), C(250,189,47),
            C(131,165,152), C(211,134,155), C(142,192,124),C(235,219,178),
        },
    },
    {
        .name = "Dracula",
        .fg = C(248, 248, 242),
        .bg = C(40, 42, 54),
        .palette = {
            C(33,34,44),    C(255,85,85),   C(80,250,123), C(241,250,140),
            C(189,147,249), C(255,121,198), C(139,233,253),C(248,248,242),
            C(98,114,164),  C(255,110,110), C(105,255,148),C(255,255,165),
            C(214,172,255), C(255,146,223), C(164,255,255),C(255,255,255),
        },
    },
    {
        .name = "Nord",
        .fg = C(216, 222, 233),
        .bg = C(46, 52, 64),
        .palette = {
            C(59,66,82),    C(191,97,106),  C(163,190,140),C(235,203,139),
            C(129,161,193), C(180,142,173), C(136,192,208),C(229,233,240),
            C(76,86,106),   C(191,97,106),  C(163,190,140),C(235,203,139),
            C(129,161,193), C(180,142,173), C(143,188,187),C(236,239,244),
        },
    },
    {
        .name = "Tokyo Night",
        .fg = C(169, 177, 214),
        .bg = C(26, 27, 38),
        .palette = {
            C(21,22,30),    C(247,118,142),C(158,206,106), C(224,175,104),
            C(122,162,247), C(187,154,247),C(125,207,255), C(169,177,214),
            C(65,72,104),   C(247,118,142),C(158,206,106), C(224,175,104),
            C(122,162,247), C(187,154,247),C(125,207,255), C(200,207,237),
        },
    },
    {
        .name = "One Dark",
        .fg = C(171, 178, 191),
        .bg = C(40, 44, 52),
        .palette = {
            C(40,44,52),    C(224,108,117), C(152,195,121),C(229,192,123),
            C(97,175,239),  C(198,120,221), C(86,182,194), C(171,178,191),
            C(92,99,112),   C(224,108,117), C(152,195,121),C(229,192,123),
            C(97,175,239),  C(198,120,221), C(86,182,194), C(200,204,212),
        },
    },
    {
        .name = "Catppuccin Mocha",
        .fg = C(205, 214, 244),
        .bg = C(30, 30, 46),
        .palette = {
            C(69,71,90),    C(243,139,168), C(166,227,161),C(249,226,175),
            C(137,180,250), C(203,166,247), C(148,226,213),C(186,194,222),
            C(88,91,112),   C(243,139,168), C(166,227,161),C(249,226,175),
            C(137,180,250), C(203,166,247), C(148,226,213),C(205,214,244),
        },
    },
};

#undef C

static const gint num_schemes = G_N_ELEMENTS(schemes);

gint
bt_color_schemes_count(void)
{
    return num_schemes;
}

const BtColorScheme *
bt_color_scheme_get(gint index)
{
    if (index < 0 || index >= num_schemes)
        return &schemes[0];
    return &schemes[index];
}

gint
bt_color_scheme_find(const gchar *name)
{
    if (!name)
        return -1;
    for (gint i = 0; i < num_schemes; i++) {
        if (g_strcmp0(schemes[i].name, name) == 0)
            return i;
    }
    return -1;
}

const gchar *
bt_color_label(gint index)
{
    if (index < 0 || index >= BT_PALETTE_SIZE)
        return "?";
    return color_labels[index];
}

/* --- Geany theme import --- */

/*
 * Geany colorscheme .conf files have:
 *   [named_colors]   - color aliases: name=#RRGGBB
 *   [named_styles]   - style defs: name=fg;bg;bold;italic
 *                       fg/bg can be a named_color, another style name, or a hex color
 *
 * We parse the active scheme, resolve colors, then map syntax styles to ANSI palette:
 *   Black/BrightBlack   = default bg / darkened
 *   Red/BrightRed       = error or keyword (hot_pink in monokai)
 *   Green/BrightGreen   = string or type
 *   Yellow/BrightYellow = number
 *   Blue/BrightBlue     = keyword_2
 *   Magenta/BrightMagenta = preprocessor or keyword
 *   Cyan/BrightCyan     = comment
 *   White/BrightWhite   = default fg / brightened
 */

/* Clamp a color channel and lighten/darken */
static gdouble
clamp01(gdouble v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

static GdkRGBA
lighten(const GdkRGBA *c, gdouble amount)
{
    GdkRGBA r = {
        clamp01(c->red   + amount),
        clamp01(c->green + amount),
        clamp01(c->blue  + amount),
        1.0
    };
    return r;
}

static GdkRGBA
darken(const GdkRGBA *c, gdouble amount)
{
    return lighten(c, -amount);
}

/* Extract the fg part from a style value string like "fg;bg;bold;italic"
 * or "other_style,bold". Returns a newly-allocated string (just the fg token). */
static gchar *
extract_style_fg(const gchar *style_val)
{
    gchar *copy = g_strdup(style_val);

    /* Strip comma modifiers: "comment,bold" -> "comment" */
    gchar *comma = strchr(copy, ',');
    if (comma)
        *comma = '\0';

    /* Take part before first semicolon */
    gchar *semi = strchr(copy, ';');
    if (semi)
        *semi = '\0';

    g_strstrip(copy);
    return copy;
}

/* Extract the bg part from a style value string. Returns newly-allocated string or NULL. */
static gchar *
extract_style_bg(const gchar *style_val)
{
    gchar *copy = g_strdup(style_val);

    /* Strip comma modifiers */
    gchar *comma = strchr(copy, ',');
    if (comma)
        *comma = '\0';

    gchar *semi = strchr(copy, ';');
    if (!semi) {
        g_free(copy);
        return NULL;
    }

    gchar *bg_start = semi + 1;
    gchar *semi2 = strchr(bg_start, ';');
    if (semi2)
        *semi2 = '\0';

    gchar *result = g_strstrip(bg_start);
    if (result[0] == '\0') {
        g_free(copy);
        return NULL;
    }

    result = g_strdup(result);
    g_free(copy);
    return result;
}

/* Resolve a color token against named_colors, then named_styles (fg only).
 * Handles chains like: keyword_2 -> light_cyan -> #66d9ef
 * max_depth prevents infinite loops.
 * token must be a const string — this function never modifies it. */
static gboolean
resolve_color(GKeyFile *kf, const gchar *token, GdkRGBA *out, gint max_depth)
{
    if (!token || token[0] == '\0' || max_depth <= 0)
        return FALSE;

    /* Direct hex color */
    if (token[0] == '#') {
        /* Geany supports short hex: #RGB -> expand to #RRGGBB */
        gsize len = strlen(token + 1);
        if (len == 3) {
            gchar expanded[8];
            g_snprintf(expanded, sizeof(expanded), "#%c%c%c%c%c%c",
                       token[1], token[1], token[2], token[2], token[3], token[3]);
            gboolean ok = gdk_rgba_parse(out, expanded);
            if (ok) out->alpha = 1.0;
            return ok;
        }
        gboolean ok = gdk_rgba_parse(out, token);
        if (ok) out->alpha = 1.0;
        return ok;
    }

    /* Try named_colors first */
    gchar *color_val = g_key_file_get_string(kf, "named_colors", token, NULL);
    if (color_val) {
        gchar *stripped = g_strstrip(color_val);
        gboolean ok = resolve_color(kf, stripped, out, max_depth - 1);
        g_free(color_val);
        return ok;
    }

    /* Try named_styles — take the fg part (before first ;) */
    gchar *style_val = g_key_file_get_string(kf, "named_styles", token, NULL);
    if (style_val) {
        gchar *fg_token = extract_style_fg(style_val);
        g_free(style_val);

        gboolean ok = FALSE;
        if (fg_token[0] != '\0')
            ok = resolve_color(kf, fg_token, out, max_depth - 1);
        g_free(fg_token);
        return ok;
    }

    return FALSE;
}

/* Resolve the bg part of a named_style (after first ;) */
static gboolean
resolve_style_bg(GKeyFile *kf, const gchar *style_name, GdkRGBA *out)
{
    gchar *style_val = g_key_file_get_string(kf, "named_styles", style_name, NULL);
    if (!style_val)
        return FALSE;

    gchar *bg_token = extract_style_bg(style_val);
    g_free(style_val);

    if (!bg_token)
        return FALSE;

    gboolean ok = resolve_color(kf, bg_token, out, 10);
    g_free(bg_token);
    return ok;
}

gboolean
bt_import_geany_theme(const gchar *scheme_file,
                      const gchar *geany_config_dir,
                      GdkRGBA *fg, GdkRGBA *bg,
                      GdkRGBA palette[BT_PALETTE_SIZE])
{
    /* Locate the .conf file */
    gchar *scheme_path = NULL;
    if (!scheme_file || scheme_file[0] == '\0') {
        /* No custom scheme — Geany uses built-in defaults; try default.conf */
        scheme_path = g_build_filename(
            "/usr/share/geany/colorschemes", "default.conf", NULL);
        if (!g_file_test(scheme_path, G_FILE_TEST_EXISTS)) {
            g_free(scheme_path);
            return FALSE;
        }
    } else {
        /* Check user dir first, then system dir */
        scheme_path = g_build_filename(
            geany_config_dir, "colorschemes", scheme_file, NULL);
        if (!g_file_test(scheme_path, G_FILE_TEST_EXISTS)) {
            g_free(scheme_path);
            scheme_path = g_build_filename(
                "/usr/share/geany/colorschemes", scheme_file, NULL);
        }
    }

    if (!g_file_test(scheme_path, G_FILE_TEST_EXISTS)) {
        g_free(scheme_path);
        return FALSE;
    }

    /* 3. Parse the colorscheme .conf */
    GKeyFile *kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, scheme_path, G_KEY_FILE_NONE, NULL)) {
        g_key_file_free(kf);
        g_free(scheme_path);
        return FALSE;
    }
    g_free(scheme_path);

    /* 4. Resolve default style for fg/bg */
    GdkRGBA def_fg = {0.86, 0.86, 0.86, 1.0};  /* fallback light grey */
    GdkRGBA def_bg = {0.18, 0.18, 0.18, 1.0};   /* fallback dark grey */

    resolve_color(kf, "default", &def_fg, 10);
    resolve_style_bg(kf, "default", &def_bg);

    *fg = def_fg;
    *bg = def_bg;

    /* 5. Map syntax style foreground colors to ANSI palette.
     * We try multiple style names for each slot, taking the first that resolves. */

    /* Helper: try to resolve fg of style_name into dest */
    #define TRY_STYLE(dest, name) resolve_color(kf, (name), &(dest), 10)

    GdkRGBA col;

    /* 0: Black = bg */
    palette[0] = def_bg;

    /* 1: Red — error bg (often reddish), or line_removed, or fallback */
    col = (GdkRGBA){0.8, 0.0, 0.0, 1.0};
    {
        GdkRGBA err_bg;
        if (resolve_style_bg(kf, "error", &err_bg)) {
            /* Use error bg if it's reddish (red channel dominant) */
            if (err_bg.red > 0.3 && err_bg.red > err_bg.green && err_bg.red > err_bg.blue)
                col = err_bg;
        }
        /* If error bg wasn't useful, try line_removed or preprocessor */
        if (col.red == 0.8 && col.green == 0.0) {
            GdkRGBA tmp;
            if (TRY_STYLE(tmp, "line_removed") && tmp.red > 0.4)
                col = tmp;
            else if (TRY_STYLE(tmp, "preprocessor") && tmp.red > 0.4)
                col = tmp;
        }
    }
    palette[1] = col;

    /* 2: Green — string */
    if (!TRY_STYLE(col, "string"))
        col = (GdkRGBA){0.3, 0.6, 0.02, 1.0};
    palette[2] = col;

    /* 3: Yellow — number */
    if (!TRY_STYLE(col, "number") && !TRY_STYLE(col, "number_1"))
        col = (GdkRGBA){0.77, 0.63, 0.0, 1.0};
    palette[3] = col;

    /* 4: Blue — keyword */
    if (!TRY_STYLE(col, "keyword") && !TRY_STYLE(col, "keyword_1"))
        col = (GdkRGBA){0.2, 0.4, 0.64, 1.0};
    palette[4] = col;

    /* 5: Magenta — type or keyword_2 */
    if (!TRY_STYLE(col, "type") && !TRY_STYLE(col, "keyword_2"))
        col = (GdkRGBA){0.46, 0.31, 0.48, 1.0};
    palette[5] = col;

    /* 6: Cyan — keyword_2 or comment */
    if (!TRY_STYLE(col, "keyword_2") && !TRY_STYLE(col, "comment"))
        col = (GdkRGBA){0.02, 0.6, 0.6, 1.0};
    palette[6] = col;

    /* 7: White = fg */
    palette[7] = def_fg;

    /* 8-15: Bright variants — lighten the normal colors */
    palette[8]  = lighten(&palette[0], 0.15);  /* Bright Black */
    palette[9]  = lighten(&palette[1], 0.12);  /* Bright Red */
    palette[10] = lighten(&palette[2], 0.12);  /* Bright Green */
    palette[11] = lighten(&palette[3], 0.12);  /* Bright Yellow */
    palette[12] = lighten(&palette[4], 0.12);  /* Bright Blue */
    palette[13] = lighten(&palette[5], 0.12);  /* Bright Magenta */
    palette[14] = lighten(&palette[6], 0.12);  /* Bright Cyan */
    palette[15] = lighten(&palette[7], 0.10);  /* Bright White */

    /* For light themes (bright bg), darken the bright-black instead */
    gdouble bg_lum = def_bg.red * 0.299 + def_bg.green * 0.587 + def_bg.blue * 0.114;
    if (bg_lum > 0.5) {
        palette[8] = darken(&palette[0], 0.10);
    }

    #undef TRY_STYLE

    g_key_file_free(kf);
    return TRUE;
}
