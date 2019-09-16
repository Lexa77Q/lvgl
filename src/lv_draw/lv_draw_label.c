/**
 * @file lv_draw_label.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "lv_draw_label.h"
#include "../lv_misc/lv_math.h"
#include "../lv_hal/lv_hal_disp.h"
#include "../lv_core/lv_refr.h"

/*********************
 *      DEFINES
 *********************/
#define LABEL_RECOLOR_PAR_LENGTH 6
#define LV_LABEL_HINT_UPDATE_TH 1024 /*Update the "hint" if the label's y coordinates have changed more then this*/


/**********************
 *      TYPEDEFS
 **********************/
enum {
    CMD_STATE_WAIT,
    CMD_STATE_PAR,
    CMD_STATE_IN,
};
typedef uint8_t cmd_state_t;

/**********************
 *  STATIC PROTOTYPES
 **********************/
static void lv_draw_letter(const lv_point_t * pos_p, const lv_area_t * clip_area, const lv_font_t * font_p, uint32_t letter,
        lv_color_t color, lv_opa_t opa);

static uint8_t hex_char_to_num(char hex);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Write a text
 * @param coords coordinates of the label
 * @param mask the label will be drawn only in this area
 * @param style pointer to a style
 * @param opa_scale scale down all opacities by the factor
 * @param txt 0 terminated text to write
 * @param flag settings for the text from 'txt_flag_t' enum
 * @param offset text offset in x and y direction (NULL if unused)
 * @param sel_start start index of selected area (`LV_LABEL_TXT_SEL_OFF` if none)
 * @param sel_end end index of selected area (`LV_LABEL_TXT_SEL_OFF` if none)
 */
void lv_draw_label(const lv_area_t * coords, const lv_area_t * mask, const lv_style_t * style, lv_opa_t opa_scale,
                   const char * txt, lv_txt_flag_t flag, lv_point_t * offset, uint16_t sel_start, uint16_t sel_end,
                   lv_draw_label_hint_t * hint)
{
    const lv_font_t * font = style->text.font;
    lv_coord_t w;
    if((flag & LV_TXT_FLAG_EXPAND) == 0) {
        /*Normally use the label's width as width*/
        w = lv_area_get_width(coords);
    } else {
        /*If EXAPND is enabled then not limit the text's width to the object's width*/
        lv_point_t p;
        lv_txt_get_size(&p, txt, style->text.font, style->text.letter_space, style->text.line_space, LV_COORD_MAX,
                        flag);
        w = p.x;
    }

    lv_coord_t line_height = lv_font_get_line_height(font) + style->text.line_space;

    /*Init variables for the first line*/
    lv_coord_t line_width = 0;
    lv_point_t pos;
    pos.x = coords->x1;
    pos.y = coords->y1;

    lv_coord_t x_ofs = 0;
    lv_coord_t y_ofs = 0;
    if(offset != NULL) {
        x_ofs = offset->x;
        y_ofs = offset->y;
        pos.y += y_ofs;
    }

    uint32_t line_start     = 0;
    int32_t last_line_start = -1;

    /*Check the hint to use the cached info*/
    if(hint && y_ofs == 0 && coords->y1 < 0) {
        /*If the label changed too much recalculate the hint.*/
        if(LV_MATH_ABS(hint->coord_y - coords->y1) > LV_LABEL_HINT_UPDATE_TH - 2 * line_height) {
            hint->line_start = -1;
        }
        last_line_start = hint->line_start;
    }

    /*Use the hint if it's valid*/
    if(hint && last_line_start >= 0) {
        line_start = last_line_start;
        pos.y += hint->y;
    }

    uint32_t line_end = line_start + lv_txt_get_next_line(&txt[line_start], font, style->text.letter_space, w, flag);

    /*Go the first visible line*/
    while(pos.y + line_height < mask->y1) {
        /*Go to next line*/
        line_start = line_end;
        line_end += lv_txt_get_next_line(&txt[line_start], font, style->text.letter_space, w, flag);
        pos.y += line_height;

        /*Save at the threshold coordinate*/
        if(hint && pos.y >= -LV_LABEL_HINT_UPDATE_TH && hint->line_start < 0) {
            hint->line_start = line_start;
            hint->y          = pos.y - coords->y1;
            hint->coord_y    = coords->y1;
        }

        if(txt[line_start] == '\0') return;
    }

    /*Align to middle*/
    if(flag & LV_TXT_FLAG_CENTER) {
        line_width = lv_txt_get_width(&txt[line_start], line_end - line_start, font, style->text.letter_space, flag);

        pos.x += (lv_area_get_width(coords) - line_width) / 2;

    }
    /*Align to the right*/
    else if(flag & LV_TXT_FLAG_RIGHT) {
        line_width = lv_txt_get_width(&txt[line_start], line_end - line_start, font, style->text.letter_space, flag);
        pos.x += lv_area_get_width(coords) - line_width;
    }

    lv_opa_t opa = opa_scale == LV_OPA_COVER ? style->text.opa : (uint16_t)((uint16_t)style->text.opa * opa_scale) >> 8;

    cmd_state_t cmd_state = CMD_STATE_WAIT;
    uint32_t i;
    uint16_t par_start = 0;
    lv_color_t recolor;
    lv_coord_t letter_w;
    lv_style_t sel_style;
    lv_style_copy(&sel_style, &lv_style_plain_color);
    sel_style.body.main_color = sel_style.body.grad_color = style->text.sel_color;

    /*Write out all lines*/
    while(txt[line_start] != '\0') {
        if(offset != NULL) {
            pos.x += x_ofs;
        }
        /*Write all letter of a line*/
        cmd_state = CMD_STATE_WAIT;
        i         = line_start;
        uint32_t letter;
        uint32_t letter_next;
        while(i < line_end) {
            letter      = lv_txt_encoded_next(txt, &i);
            letter_next = lv_txt_encoded_next(&txt[i], NULL);

            /*Handle the re-color command*/
            if((flag & LV_TXT_FLAG_RECOLOR) != 0) {
                if(letter == (uint32_t)LV_TXT_COLOR_CMD[0]) {
                    if(cmd_state == CMD_STATE_WAIT) { /*Start char*/
                        par_start = i;
                        cmd_state = CMD_STATE_PAR;
                        continue;
                    } else if(cmd_state == CMD_STATE_PAR) { /*Other start char in parameter escaped cmd. char */
                        cmd_state = CMD_STATE_WAIT;
                    } else if(cmd_state == CMD_STATE_IN) { /*Command end */
                        cmd_state = CMD_STATE_WAIT;
                        continue;
                    }
                }

                /*Skip the color parameter and wait the space after it*/
                if(cmd_state == CMD_STATE_PAR) {
                    if(letter == ' ') {
                        /*Get the parameter*/
                        if(i - par_start == LABEL_RECOLOR_PAR_LENGTH + 1) {
                            char buf[LABEL_RECOLOR_PAR_LENGTH + 1];
                            memcpy(buf, &txt[par_start], LABEL_RECOLOR_PAR_LENGTH);
                            buf[LABEL_RECOLOR_PAR_LENGTH] = '\0';
                            int r, g, b;
                            r       = (hex_char_to_num(buf[0]) << 4) + hex_char_to_num(buf[1]);
                            g       = (hex_char_to_num(buf[2]) << 4) + hex_char_to_num(buf[3]);
                            b       = (hex_char_to_num(buf[4]) << 4) + hex_char_to_num(buf[5]);
                            recolor = lv_color_make(r, g, b);
                        } else {
                            recolor.full = style->text.color.full;
                        }
                        cmd_state = CMD_STATE_IN; /*After the parameter the text is in the command*/
                    }
                    continue;
                }
            }

            lv_color_t color = style->text.color;

            if(cmd_state == CMD_STATE_IN) color = recolor;

            letter_w = lv_font_get_glyph_width(font, letter, letter_next);

            if(sel_start != 0xFFFF && sel_end != 0xFFFF) {
                int char_ind = lv_encoded_get_char_id(txt, i);
                /*Do not draw the rectangle on the character at `sel_start`.*/
                if(char_ind > sel_start && char_ind <= sel_end) {
                    lv_area_t sel_coords;
                    sel_coords.x1 = pos.x;
                    sel_coords.y1 = pos.y;
                    sel_coords.x2 = pos.x + letter_w + style->text.letter_space - 1;
                    sel_coords.y2 = pos.y + line_height - 1;
                    lv_draw_rect(&sel_coords, mask, &sel_style, opa);
                }
            }
            lv_draw_letter(&pos, mask, font, letter, color, opa);

            if(letter_w > 0) {
                pos.x += letter_w + style->text.letter_space;
            }
        }
        /*Go to next line*/
        line_start = line_end;
        line_end += lv_txt_get_next_line(&txt[line_start], font, style->text.letter_space, w, flag);

        pos.x = coords->x1;
        /*Align to middle*/
        if(flag & LV_TXT_FLAG_CENTER) {
            line_width =
                lv_txt_get_width(&txt[line_start], line_end - line_start, font, style->text.letter_space, flag);

            pos.x += (lv_area_get_width(coords) - line_width) / 2;

        }
        /*Align to the right*/
        else if(flag & LV_TXT_FLAG_RIGHT) {
            line_width =
                lv_txt_get_width(&txt[line_start], line_end - line_start, font, style->text.letter_space, flag);
            pos.x += lv_area_get_width(coords) - line_width;
        }

        /*Go the next line position*/
        pos.y += line_height;

        if(pos.y > mask->y2) return;
    }
}

/**********************
 *   STATIC FUNCTIONS
 **********************/


/**
 * Draw a letter in the Virtual Display Buffer
 * @param pos_p left-top coordinate of the latter
 * @param mask_p the letter will be drawn only on this area  (truncated to VDB area)
 * @param font_p pointer to font
 * @param letter a letter to draw
 * @param color color of letter
 * @param opa opacity of letter (0..255)
 */
static void lv_draw_letter(const lv_point_t * pos_p, const lv_area_t * clip_area, const lv_font_t * font_p, uint32_t letter,
        lv_color_t color, lv_opa_t opa)
{
    /*clang-format off*/
    const uint8_t bpp1_opa_table[2]  = {0, 255};          /*Opacity mapping with bpp = 1 (Just for compatibility)*/
    const uint8_t bpp2_opa_table[4]  = {0, 85, 170, 255}; /*Opacity mapping with bpp = 2*/
    const uint8_t bpp4_opa_table[16] = {0,  17, 34,  51,  /*Opacity mapping with bpp = 4*/
            68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};
    /*clang-format on*/

    if(opa < LV_OPA_MIN) return;
    if(opa > LV_OPA_MAX) opa = LV_OPA_COVER;

    if(font_p == NULL) {
        LV_LOG_WARN("lv_draw_letter: font is NULL");
        return;
    }

    lv_font_glyph_dsc_t g;
    bool g_ret = lv_font_get_glyph_dsc(font_p, &g, letter, '\0');
    if(g_ret == false)  {
        /* Add waring if the dsc is not found
         * but do not print warning for non printable ASCII chars (e.g. '\n')*/
        if(letter >= 0x20) {
            LV_LOG_WARN("lv_draw_letter: glyph dsc. not found");
        }
        return;
    }

    lv_coord_t pos_x = pos_p->x + g.ofs_x;
    lv_coord_t pos_y = pos_p->y + (font_p->line_height - font_p->base_line) - g.box_h - g.ofs_y;

    const uint8_t * bpp_opa_table;
    uint8_t bitmask_init;
    uint8_t bitmask;

    if(g.bpp == 3) g.bpp = 4;

    switch(g.bpp) {
    case 1:
        bpp_opa_table = bpp1_opa_table;
        bitmask_init  = 0x80;
        break;
    case 2:
        bpp_opa_table = bpp2_opa_table;
        bitmask_init  = 0xC0;
        break;
    case 4:
        bpp_opa_table = bpp4_opa_table;
        bitmask_init  = 0xF0;
        break;
    case 8:
        bpp_opa_table = NULL;
        bitmask_init  = 0xFF;
        break;       /*No opa table, pixel value will be used directly*/
    default:
        LV_LOG_WARN("lv_draw_letter: invalid bpp not found");
        return; /*Invalid bpp. Can't render the letter*/
    }

    const uint8_t * map_p = lv_font_get_glyph_bitmap(font_p, letter);
    if(map_p == NULL) {
        LV_LOG_WARN("lv_draw_letter: character's bitmap not found");
        return;
    }

    /*If the letter is completely out of mask don't draw it */
    if(pos_x + g.box_w < clip_area->x1 ||
            pos_x > clip_area->x2 ||
            pos_y + g.box_h < clip_area->y1 ||
            pos_y > clip_area->y2) return;

    lv_coord_t col, row;

    uint8_t width_byte_scr = g.box_w >> 3; /*Width in bytes (on the screen finally) (e.g. w = 11 -> 2 bytes wide)*/
    if(g.box_w & 0x7) width_byte_scr++;
    uint16_t width_bit = g.box_w * g.bpp; /*Letter width in bits*/

    /* Calculate the col/row start/end on the map*/
    lv_coord_t col_start = pos_x >= clip_area->x1 ? 0 : clip_area->x1 - pos_x;
    lv_coord_t col_end   = pos_x + g.box_w <= clip_area->x2 ? g.box_w : clip_area->x2 - pos_x + 1;
    lv_coord_t row_start = pos_y >= clip_area->y1 ? 0 : clip_area->y1 - pos_y;
    lv_coord_t row_end   = pos_y + g.box_h <= clip_area->y2 ? g.box_h : clip_area->y2 - pos_y + 1;

    /*Move on the map too*/
    uint32_t bit_ofs = (row_start * width_bit) + (col_start * g.bpp);
    map_p += bit_ofs >> 3;

    uint8_t letter_px;
    lv_opa_t px_opa;
    uint16_t col_bit;
    col_bit = bit_ofs & 0x7; /* "& 0x7" equals to "% 8" just faster */

    uint32_t mask_buf_size = g.box_w * g.box_h > LV_HOR_RES_MAX ? g.box_w * g.box_h : LV_HOR_RES_MAX;
    lv_opa_t * mask_buf = lv_draw_buf_get(mask_buf_size);
    lv_coord_t mask_p = 0;
    lv_coord_t mask_p_start;

    lv_area_t fill_area;
    fill_area.x1 = col_start + pos_x;
    fill_area.x2 = col_end  + pos_x - 1;
    fill_area.y1 = row_start + pos_y;
    fill_area.y2 = fill_area.y1;

    uint8_t other_mask_cnt = lv_draw_mask_get_cnt();

    for(row = row_start ; row < row_end; row++) {
        bitmask = bitmask_init >> col_bit;
        mask_p_start = mask_p;
        for(col = col_start; col < col_end; col++) {

            /*Load the pixel's opacity into the mask*/
            letter_px = (*map_p & bitmask) >> (8 - col_bit - g.bpp);
            if(letter_px != 0) {
                if(opa == LV_OPA_COVER) {
                    px_opa = g.bpp == 8 ? letter_px : bpp_opa_table[letter_px];
                } else {
                    px_opa = g.bpp == 8 ? (uint16_t)((uint16_t)letter_px * opa) >> 8
                            : (uint16_t)((uint16_t)bpp_opa_table[letter_px] * opa) >> 8;
                }

                mask_buf[mask_p] = px_opa;

            } else {
                mask_buf[mask_p] = 0;
            }

            /*Go to the next column*/
            if(col_bit < 8 - g.bpp) {
                col_bit += g.bpp;
                bitmask = bitmask >> g.bpp;
            } else {
                col_bit = 0;
                bitmask = bitmask_init;
                map_p++;
            }

            /*Next mask byte*/
            mask_p++;
        }

        /*Apply masks if any*/
        if(other_mask_cnt) {
            lv_draw_mask_res_t mask_res = lv_draw_mask_apply(mask_buf + mask_p_start, fill_area.x1, fill_area.y2, lv_area_get_width(&fill_area));
            if(mask_res == LV_DRAW_MASK_RES_FULL_TRANSP) {
                memset(mask_buf + mask_p_start, 0x00, lv_area_get_width(&fill_area));
            }
        }

        if((uint32_t) mask_p + (row_end - row_start) < mask_buf_size) {
            fill_area.y2 ++;
        } else {
            lv_blend_fill(clip_area, &fill_area,
                    color, mask_buf, LV_DRAW_MASK_RES_CHANGED, opa,
                    LV_BLEND_MODE_NORMAL);

            fill_area.y1 = fill_area.y2 + 1;
            fill_area.y2 = fill_area.y1;
            mask_p = 0;
        }

        col_bit += ((g.box_w - col_end) + col_start) * g.bpp;

        map_p += (col_bit >> 3);
        col_bit = col_bit & 0x7;
    }

    /*Flush the last part*/
    if(fill_area.y1 != fill_area.y2) {
        fill_area.y2--;
        lv_blend_fill(clip_area, &fill_area,
                color, mask_buf, LV_DRAW_MASK_RES_CHANGED, opa,
                LV_BLEND_MODE_NORMAL);
        mask_p = 0;
    }

    lv_draw_buf_release(mask_buf);
}


/**
 * Convert a hexadecimal characters to a number (0..15)
 * @param hex Pointer to a hexadecimal character (0..9, A..F)
 * @return the numerical value of `hex` or 0 on error
 */
static uint8_t hex_char_to_num(char hex)
{
    uint8_t result = 0;

    if(hex >= '0' && hex <= '9') {
        result = hex - '0';
    } else {
        if(hex >= 'a') hex -= 'a' - 'A'; /*Convert to upper case*/

        switch(hex) {
            case 'A': result = 10; break;
            case 'B': result = 11; break;
            case 'C': result = 12; break;
            case 'D': result = 13; break;
            case 'E': result = 14; break;
            case 'F': result = 15; break;
            default: result = 0; break;
        }
    }

    return result;
}
