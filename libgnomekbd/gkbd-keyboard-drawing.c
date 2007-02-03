/*
 * Copyright (C) 2006 Sergey V. Udaltsov <svu@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XKBgeom.h>
#include <stdlib.h>
#include <memory.h>
#include <math.h>

#include <gkbd-keyboard-drawing.h>
#include <gkbd-keyboard-drawing-marshal.h>

#define noKBDRAW_DEBUG

enum {
	BAD_KEYCODE = 0,
	NUM_SIGNALS
};

static guint gkbd_keyboard_drawing_signals[NUM_SIGNALS] = { 0 };

static void gkbd_keyboard_drawing_set_mods (GkbdKeyboardDrawing * drawing,
					    guint mods);

static gint
xkb_to_pixmap_coord (GkbdKeyboardDrawing * drawing, gint n)
{
	return n * drawing->scale_numerator / drawing->scale_denominator;
}

/* angle is in tenths of a degree; coordinates can be anything as (xkb,
 * pixels, pango) as long as they are all the same */
static void
rotate_coordinate (gint origin_x,
		   gint origin_y,
		   gint x,
		   gint y, gint angle, gint * rotated_x, gint * rotated_y)
{
	*rotated_x =
	    origin_x + (x - origin_x) * cos (M_PI * angle / 1800.0) - (y -
								       origin_y)
	    * sin (M_PI * angle / 1800.0);
	*rotated_y =
	    origin_y + (x - origin_x) * sin (M_PI * angle / 1800.0) + (y -
								       origin_y)
	    * cos (M_PI * angle / 1800.0);
}

static void
draw_polygon (GkbdKeyboardDrawing * drawing,
	      GdkColor * fill_color,
	      gint xkb_x,
	      gint xkb_y, XkbPointRec * xkb_points, guint num_points)
{
	GtkStateType state = GTK_WIDGET_STATE (GTK_WIDGET (drawing));
	GdkGC *gc;
	GdkPoint *points;
	gboolean filled;
	gint i;

	if (drawing->pixmap == NULL)
		return;

	if (fill_color) {
		gc = gdk_gc_new (GTK_WIDGET (drawing)->window);
		gdk_gc_set_rgb_fg_color (gc, fill_color);
		filled = TRUE;
	} else {
		gc = GTK_WIDGET (drawing)->style->dark_gc[state];
		filled = FALSE;
	}

	points = g_new (GdkPoint, num_points);

	for (i = 0; i < num_points; i++) {
		points[i].x =
		    xkb_to_pixmap_coord (drawing, xkb_x + xkb_points[i].x);
		points[i].y =
		    xkb_to_pixmap_coord (drawing, xkb_y + xkb_points[i].y);
	}

	gdk_draw_polygon (drawing->pixmap, gc, filled, points, num_points);

	g_free (points);
	if (fill_color)
		g_object_unref (gc);
}

/* x, y, width, height are in the xkb coordinate system */
static void
draw_rectangle (GkbdKeyboardDrawing * drawing,
		GdkColor * fill_color,
		gint angle,
		gint xkb_x, gint xkb_y, gint xkb_width, gint xkb_height)
{
	if (drawing->pixmap == NULL)
		return;

	if (angle == 0) {
		GtkStateType state =
		    GTK_WIDGET_STATE (GTK_WIDGET (drawing));
		gint x, y, width, height;
		gboolean filled;
		GdkGC *gc;

		if (fill_color) {
			gc = gdk_gc_new (GTK_WIDGET (drawing)->window);
			gdk_gc_set_rgb_fg_color (gc, fill_color);
			filled = TRUE;
		} else {
			gc = GTK_WIDGET (drawing)->style->dark_gc[state];
			filled = FALSE;
		}

		x = xkb_to_pixmap_coord (drawing, xkb_x);
		y = xkb_to_pixmap_coord (drawing, xkb_y);
		width =
		    xkb_to_pixmap_coord (drawing, xkb_x + xkb_width) - x;
		height =
		    xkb_to_pixmap_coord (drawing, xkb_y + xkb_height) - y;

		gdk_draw_rectangle (drawing->pixmap, gc, filled, x, y,
				    width, height);

		if (fill_color)
			g_object_unref (gc);
	} else {
		XkbPointRec points[4];
		gint x, y;

		points[0].x = xkb_x;
		points[0].y = xkb_y;
		rotate_coordinate (xkb_x, xkb_y, xkb_x + xkb_width, xkb_y,
				   angle, &x, &y);
		points[1].x = x;
		points[1].y = y;
		rotate_coordinate (xkb_x, xkb_y, xkb_x + xkb_width,
				   xkb_y + xkb_height, angle, &x, &y);
		points[2].x = x;
		points[2].y = y;
		rotate_coordinate (xkb_x, xkb_y, xkb_x, xkb_y + xkb_height,
				   angle, &x, &y);
		points[3].x = x;
		points[3].y = y;

		/* the points we've calculated are relative to 0,0 */
		draw_polygon (drawing, fill_color, 0, 0, points, 4);
	}
}

static void
draw_outline (GkbdKeyboardDrawing * drawing,
	      XkbOutlineRec * outline,
	      GdkColor * color, gint angle, gint origin_x, gint origin_y)
{
#ifdef KBDRAW_DEBUG
	printf ("num_points in %p: %d\n", outline, outline->num_points);
#endif

	if (outline->num_points == 1) {
		if (color)
			draw_rectangle (drawing, color, angle, origin_x,
					origin_y, outline->points[0].x,
					outline->points[0].y);

#ifdef KBDRAW_DEBUG
		printf ("points:%p\n", outline->points);
		printf ("pointsxy:%d %d\n", outline->points[0].x,
			outline->points[0].y);
#endif

		draw_rectangle (drawing, NULL, angle, origin_x, origin_y,
				outline->points[0].x,
				outline->points[0].y);
	} else if (outline->num_points == 2) {
		gint rotated_x0, rotated_y0;

		rotate_coordinate (origin_x, origin_y,
				   origin_x + outline->points[0].x,
				   origin_y + outline->points[0].y,
				   angle, &rotated_x0, &rotated_y0);
		if (color)
			draw_rectangle (drawing, color, angle, rotated_x0,
					rotated_y0, outline->points[1].x,
					outline->points[1].y);

		draw_rectangle (drawing, NULL, angle, rotated_x0,
				rotated_y0, outline->points[1].x,
				outline->points[1].y);
	} else {
		if (color)
			draw_polygon (drawing, color, origin_x, origin_y,
				      outline->points,
				      outline->num_points);

		draw_polygon (drawing, NULL, origin_x, origin_y,
			      outline->points, outline->num_points);
	}
}

/* see PSColorDef in xkbprint */
static gboolean
parse_xkb_color_spec (gchar * colorspec, GdkColor * color)
{
	glong level;

	if (g_ascii_strcasecmp (colorspec, "black") == 0) {
		color->red = 0;
		color->green = 0;
		color->blue = 0;
	} else if (g_ascii_strcasecmp (colorspec, "white") == 0) {
		color->red = 65535;
		color->green = 65535;
		color->blue = 65535;
	} else if (g_ascii_strncasecmp (colorspec, "grey", 4) == 0 ||
		   g_ascii_strncasecmp (colorspec, "gray", 4) == 0) {
		level = strtol (colorspec + 4, NULL, 10);

		color->red = 65535 - 65535 * level / 100;
		color->green = 65535 - 65535 * level / 100;
		color->blue = 65535 - 65535 * level / 100;
	} else if (g_ascii_strcasecmp (colorspec, "red") == 0) {
		color->red = 65535;
		color->green = 0;
		color->blue = 0;
	} else if (g_ascii_strcasecmp (colorspec, "green") == 0) {
		color->red = 0;
		color->green = 65535;
		color->blue = 0;
	} else if (g_ascii_strcasecmp (colorspec, "blue") == 0) {
		color->red = 0;
		color->green = 0;
		color->blue = 65535;
	} else if (g_ascii_strncasecmp (colorspec, "red", 3) == 0) {
		level = strtol (colorspec + 3, NULL, 10);

		color->red = 65535 * level / 100;
		color->green = 0;
		color->blue = 0;
	} else if (g_ascii_strncasecmp (colorspec, "green", 5) == 0) {
		level = strtol (colorspec + 5, NULL, 10);

		color->red = 0;
		color->green = 65535 * level / 100;;
		color->blue = 0;
	} else if (g_ascii_strncasecmp (colorspec, "blue", 4) == 0) {
		level = strtol (colorspec + 4, NULL, 10);

		color->red = 0;
		color->green = 0;
		color->blue = 65535 * level / 100;
	} else
		return FALSE;

	return TRUE;
}


static guint
find_keycode (GkbdKeyboardDrawing * drawing, gchar * key_name)
{
	guint i;

	if (!drawing->xkb)
		return (gint) (-1);

	for (i = drawing->xkb->min_key_code;
	     i <= drawing->xkb->max_key_code; i++) {
		if (drawing->xkb->names->keys[i].name[0] == key_name[0]
		    && drawing->xkb->names->keys[i].name[1] == key_name[1]
		    && drawing->xkb->names->keys[i].name[2] == key_name[2]
		    && drawing->xkb->names->keys[i].name[3] == key_name[3])
			return i;
	}

	return (guint) (-1);
}


static void
set_key_label_in_layout (GkbdKeyboardDrawing * drawing,
			 PangoLayout * layout, guint keyval)
{
	gchar buf[5];
	gunichar uc;

	switch (keyval) {
	case GDK_Scroll_Lock:
		pango_layout_set_text (layout, "Scroll\nLock", -1);
		break;

	case GDK_space:
		pango_layout_set_text (layout, "", -1);
		break;

	case GDK_Sys_Req:
		pango_layout_set_text (layout, "Sys Rq", -1);
		break;

	case GDK_Page_Up:
		pango_layout_set_text (layout, "Page\nUp", -1);
		break;

	case GDK_Page_Down:
		pango_layout_set_text (layout, "Page\nDown", -1);
		break;

	case GDK_Num_Lock:
		pango_layout_set_text (layout, "Num\nLock", -1);
		break;

	case GDK_KP_Page_Up:
		pango_layout_set_text (layout, "Pg Up", -1);
		break;

	case GDK_KP_Page_Down:
		pango_layout_set_text (layout, "Pg Dn", -1);
		break;

	case GDK_KP_Home:
		pango_layout_set_text (layout, "Home", -1);
		break;

	case GDK_KP_Left:
		pango_layout_set_text (layout, "Left", -1);
		break;

	case GDK_KP_End:
		pango_layout_set_text (layout, "End", -1);
		break;

	case GDK_KP_Up:
		pango_layout_set_text (layout, "Up", -1);
		break;

	case GDK_KP_Begin:
		pango_layout_set_text (layout, "Begin", -1);
		break;

	case GDK_KP_Right:
		pango_layout_set_text (layout, "Right", -1);
		break;

	case GDK_KP_Enter:
		pango_layout_set_text (layout, "Enter", -1);
		break;

	case GDK_KP_Down:
		pango_layout_set_text (layout, "Down", -1);
		break;

	case GDK_KP_Insert:
		pango_layout_set_text (layout, "Ins", -1);
		break;

	case GDK_KP_Delete:
		pango_layout_set_text (layout, "Del", -1);
		break;

	case GDK_dead_grave:
		pango_layout_set_text (layout, "ˋ", -1);
		break;

	case GDK_dead_acute:
		pango_layout_set_text (layout, "ˊ", -1);
		break;

	case GDK_dead_circumflex:
		pango_layout_set_text (layout, "ˆ", -1);
		break;

	case GDK_dead_tilde:
		pango_layout_set_text (layout, "~", -1);
		break;

	case GDK_dead_macron:
		pango_layout_set_text (layout, "ˉ", -1);
		break;

	case GDK_dead_breve:
		pango_layout_set_text (layout, "˘", -1);
		break;

	case GDK_dead_abovedot:
		pango_layout_set_text (layout, "˙", -1);
		break;

	case GDK_dead_diaeresis:
		pango_layout_set_text (layout, "¨", -1);
		break;

	case GDK_dead_abovering:
		pango_layout_set_text (layout, "˚", -1);
		break;

	case GDK_dead_doubleacute:
		pango_layout_set_text (layout, "˝", -1);
		break;

	case GDK_dead_caron:
		pango_layout_set_text (layout, "ˇ", -1);
		break;

	case GDK_dead_cedilla:
		pango_layout_set_text (layout, "¸", -1);
		break;

	case GDK_dead_ogonek:
		pango_layout_set_text (layout, "˛", -1);
		break;

		/* case GDK_dead_iota:
		 * case GDK_dead_voiced_sound:
		 * case GDK_dead_semivoiced_sound: */

	case GDK_dead_belowdot:
		pango_layout_set_text (layout, " ̣", -1);
		break;

	case GDK_horizconnector:
		pango_layout_set_text (layout, "horiz\nconn", -1);
		break;

	case GDK_Mode_switch:
		pango_layout_set_text (layout, "AltGr", -1);
		break;

	case GDK_Multi_key:
		pango_layout_set_text (layout, "Compose", -1);
		break;

	default:
		uc = gdk_keyval_to_unicode (keyval);
		if (uc != 0 && g_unichar_isgraph (uc)) {
			buf[g_unichar_to_utf8 (uc, buf)] = '\0';
			pango_layout_set_text (layout, buf, -1);
		} else {
			gchar *name = gdk_keyval_name (keyval);
			if (name)
				pango_layout_set_text (layout, name, -1);
			else
				pango_layout_set_text (layout, "", -1);
		}
	}
}


static void
draw_layout (GkbdKeyboardDrawing * drawing,
	     gint angle, gint x, gint y, PangoLayout * layout)
{
	GtkStateType state = GTK_WIDGET_STATE (GTK_WIDGET (drawing));
	PangoLayoutLine *line;
	gint x_off, y_off;
	gint i;

	if (drawing->pixmap == NULL)
		return;

	if (angle != drawing->angle) {
		PangoMatrix matrix = PANGO_MATRIX_INIT;
		pango_matrix_rotate (&matrix, -angle / 10.0);
		pango_context_set_matrix (gtk_widget_get_pango_context
					  (GTK_WIDGET (drawing)), &matrix);
		pango_layout_context_changed (drawing->layout);
		drawing->angle = angle;
	}

	i = 0;
	y_off = 0;
	for (line = pango_layout_get_line (drawing->layout, i);
	     line != NULL;
	     line = pango_layout_get_line (drawing->layout, ++i)) {
		GSList *runp;
		PangoRectangle line_extents;

		x_off = 0;

		for (runp = line->runs; runp != NULL; runp = runp->next) {
			PangoGlyphItem *run = runp->data;
			gint j;

			for (j = 0; j < run->glyphs->num_glyphs; j++) {
				PangoGlyphGeometry *geometry;
				gint xx, yy;

				geometry =
				    &run->glyphs->glyphs[j].geometry;

				rotate_coordinate (0, 0, x_off, y_off,
						   angle, &xx, &yy);
				geometry->x_offset -= x_off - xx;
				geometry->y_offset -= y_off - yy;

				x_off += geometry->width;
			}
		}

		pango_layout_line_get_extents (line, NULL, &line_extents);
		y_off +=
		    line_extents.height +
		    pango_layout_get_spacing (drawing->layout);
	}

	gdk_draw_layout (drawing->pixmap,
			 GTK_WIDGET (drawing)->style->text_gc[state], x, y,
			 drawing->layout);
}

static void
draw_key_label_helper (GkbdKeyboardDrawing * drawing,
		       KeySym keysym,
		       gint angle,
		       GkbdKeyboardDrawingGroupLevelPosition glp,
		       gint x,
		       gint y, gint width, gint height, gint padding)
{
	gint label_x, label_y, label_max_width, ycell;

	if (keysym == 0)
		return;
#ifdef KBDRAW_DEBUG
	printf ("keysym: %04X(%c) at glp: %d\n",
		(unsigned) keysym, (char) keysym, (int) glp);
#endif

	switch (glp) {
	case GKBD_KEYBOARD_DRAWING_POS_TOPLEFT:
	case GKBD_KEYBOARD_DRAWING_POS_BOTTOMLEFT:
		{
			ycell =
			    glp == GKBD_KEYBOARD_DRAWING_POS_BOTTOMLEFT;

			rotate_coordinate (x, y, x + padding,
					   y + padding + (height -
							  2 * padding) *
					   ycell * 4 / 7, angle, &label_x,
					   &label_y);
			label_max_width =
			    PANGO_SCALE * (width - 2 * padding);
			break;
		}
	case GKBD_KEYBOARD_DRAWING_POS_TOPRIGHT:
	case GKBD_KEYBOARD_DRAWING_POS_BOTTOMRIGHT:
		{
			ycell =
			    glp == GKBD_KEYBOARD_DRAWING_POS_BOTTOMRIGHT;

			rotate_coordinate (x, y,
					   x + padding + (width -
							  2 * padding) *
					   4 / 7,
					   y + padding + (height -
							  2 * padding) *
					   ycell * 4 / 7, angle, &label_x,
					   &label_y);
			label_max_width =
			    PANGO_SCALE * ((width - 2 * padding) -
					   (width - 2 * padding) * 4 / 7);
			break;
		}
	default:
		return;
	}
	set_key_label_in_layout (drawing, drawing->layout, keysym);
	pango_layout_set_width (drawing->layout, label_max_width);
	label_y -= (pango_layout_get_line_count (drawing->layout) - 1) *
		(pango_font_description_get_size (drawing->font_desc) / PANGO_SCALE);
	draw_layout (drawing, angle, label_x, label_y, drawing->layout);
}

static void
draw_key_label (GkbdKeyboardDrawing * drawing,
		guint keycode,
		gint angle,
		gint xkb_origin_x,
		gint xkb_origin_y, gint xkb_width, gint xkb_height)
{
	gint x, y, width, height;
	gint padding;
	gint g, l, glp;

	if (!drawing->xkb)
		return;

	padding = 23 * drawing->scale_numerator / drawing->scale_denominator;	/* 2.3mm */

	x = xkb_to_pixmap_coord (drawing, xkb_origin_x);
	y = xkb_to_pixmap_coord (drawing, xkb_origin_y);
	width =
	    xkb_to_pixmap_coord (drawing, xkb_origin_x + xkb_width) - x;
	height =
	    xkb_to_pixmap_coord (drawing, xkb_origin_y + xkb_height) - y;

	for (glp = GKBD_KEYBOARD_DRAWING_POS_TOPLEFT;
	     glp < GKBD_KEYBOARD_DRAWING_POS_TOTAL; glp++) {
		if (drawing->groupLevels[glp] == NULL)
			continue;
		g = drawing->groupLevels[glp]->group;
		l = drawing->groupLevels[glp]->level;

		if (g < 0 || g >= XkbKeyNumGroups (drawing->xkb, keycode))
			continue;
		if (l < 0
		    || l >= XkbKeyGroupWidth (drawing->xkb, keycode, g))
			continue;

		/* Skip "exotic" levels like the "Ctrl" level in PC_SYSREQ */
		if (l > 0) {
			guint mods = XkbKeyKeyType (drawing->xkb, keycode, g)->mods.mask;
			if ((mods & (ShiftMask | drawing->l3mod)) == 0)
				continue;
		}

		if (drawing->track_modifiers) {
			uint mods_rtrn;
			KeySym keysym;

			if (XkbTranslateKeyCode (drawing->xkb, keycode,
						 XkbBuildCoreState
						 (drawing->mods, g),
						 &mods_rtrn, &keysym)) {
				draw_key_label_helper (drawing, keysym,
						       angle, glp, x, y,
						       width, height,
						       padding);
				/* reverse y order */
			}
		} else {
			KeySym keysym;

			keysym =
			    XkbKeySymEntry (drawing->xkb, keycode, l, g);

			draw_key_label_helper (drawing, keysym, angle, glp,
					       x, y, width, height,
					       padding);
			/* reverse y order */
		}
	}
}

/* groups are from 0-3 */
static void
draw_key (GkbdKeyboardDrawing * drawing, GkbdKeyboardDrawingKey * key)
{
	XkbShapeRec *shape;
	GdkColor *color;
	gint i;

	if (!drawing->xkb)
		return;

#ifdef KBDRAW_DEBUG
	printf ("shape: %p (%p + %d)\n",
		drawing->xkb->geom->shapes + key->xkbkey->shape_ndx,
		drawing->xkb->geom->shapes, key->xkbkey->shape_ndx);
#endif

	shape = drawing->xkb->geom->shapes + key->xkbkey->shape_ndx;

	if (key->pressed)
		color =
		    &(GTK_WIDGET (drawing)->style->
		      base[GTK_STATE_SELECTED]);
	else
		color = drawing->colors + key->xkbkey->color_ndx;

#ifdef KBDRAW_DEBUG
	printf ("outlines: %p(%d)\n", shape->outlines,
		shape->num_outlines);
#endif

	for (i = 0; i < 1 /* shape->num_outlines */ ; i++)
		draw_outline (drawing, shape->outlines + i, color,
			      key->angle, key->origin_x, key->origin_y);

	draw_key_label (drawing, key->keycode, key->angle, key->origin_x,
			key->origin_y, shape->bounds.x2, shape->bounds.y2);
}

static void
invalidate_region (GkbdKeyboardDrawing * drawing,
		   gdouble angle,
		   gint origin_x, gint origin_y, XkbShapeRec * shape)
{
	GdkPoint points[4];
	gint x_min, x_max, y_min, y_max;
	gint x, y, width, height;
	gint xx, yy;

	rotate_coordinate (0, 0, 0, 0, angle, &xx, &yy);
	points[0].x = xx;
	points[0].y = yy;
	rotate_coordinate (0, 0, shape->bounds.x2, 0, angle, &xx, &yy);
	points[1].x = xx;
	points[1].y = yy;
	rotate_coordinate (0, 0, shape->bounds.x2, shape->bounds.y2, angle,
			   &xx, &yy);
	points[2].x = xx;
	points[2].y = yy;
	rotate_coordinate (0, 0, 0, shape->bounds.y2, angle, &xx, &yy);
	points[3].x = xx;
	points[3].y = yy;

	x_min =
	    MIN (MIN (points[0].x, points[1].x),
		 MIN (points[2].x, points[3].x));
	x_max =
	    MAX (MAX (points[0].x, points[1].x),
		 MAX (points[2].x, points[3].x));
	y_min =
	    MIN (MIN (points[0].y, points[1].y),
		 MIN (points[2].y, points[3].y));
	y_max =
	    MAX (MAX (points[0].y, points[1].y),
		 MAX (points[2].y, points[3].y));

	x = xkb_to_pixmap_coord (drawing, origin_x + x_min) - 6;
	y = xkb_to_pixmap_coord (drawing, origin_y + y_min) - 6;
	width = xkb_to_pixmap_coord (drawing, x_max - x_min) + 12;
	height = xkb_to_pixmap_coord (drawing, y_max - y_min) + 12;

	gtk_widget_queue_draw_area (GTK_WIDGET (drawing), x, y, width,
				    height);
}

static void
invalidate_indicator_doodad_region (GkbdKeyboardDrawing * drawing,
				    GkbdKeyboardDrawingDoodad * doodad)
{
	if (!drawing->xkb)
		return;

	invalidate_region (drawing,
			   doodad->angle,
			   doodad->origin_x +
			   doodad->doodad->indicator.left,
			   doodad->origin_y +
			   doodad->doodad->indicator.top,
			   &drawing->xkb->geom->shapes[doodad->doodad->
						       indicator.
						       shape_ndx]);
}

static void
invalidate_key_region (GkbdKeyboardDrawing * drawing,
		       GkbdKeyboardDrawingKey * key)
{
	if (!drawing->xkb)
		return;

	invalidate_region (drawing,
			   key->angle,
			   key->origin_x,
			   key->origin_y,
			   &drawing->xkb->geom->shapes[key->xkbkey->
						       shape_ndx]);
}

static void
draw_text_doodad (GkbdKeyboardDrawing * drawing,
		  GkbdKeyboardDrawingDoodad * doodad,
		  XkbTextDoodadRec * text_doodad)
{
	gint x, y;
	if (!drawing->xkb)
		return;

	x = xkb_to_pixmap_coord (drawing,
				 doodad->origin_x + text_doodad->left);
	y = xkb_to_pixmap_coord (drawing,
				 doodad->origin_y + text_doodad->top);

	pango_layout_set_text (drawing->layout, text_doodad->text, -1);
	draw_layout (drawing, doodad->angle, x, y, drawing->layout);
}

static void
draw_indicator_doodad (GkbdKeyboardDrawing * drawing,
		       GkbdKeyboardDrawingDoodad * doodad,
		       XkbIndicatorDoodadRec * indicator_doodad)
{
	GdkColor *color;
	XkbShapeRec *shape;
	gint i;

	if (!drawing->xkb)
		return;

	shape = drawing->xkb->geom->shapes + indicator_doodad->shape_ndx;

	color = drawing->colors + (doodad->on ?
				   indicator_doodad->on_color_ndx :
				   indicator_doodad->off_color_ndx);

	for (i = 0; i < 1; i++)
		draw_outline (drawing, shape->outlines + i, color,
			      doodad->angle,
			      doodad->origin_x + indicator_doodad->left,
			      doodad->origin_y + indicator_doodad->top);
}

static void
draw_shape_doodad (GkbdKeyboardDrawing * drawing,
		   GkbdKeyboardDrawingDoodad * doodad,
		   XkbShapeDoodadRec * shape_doodad)
{
	XkbShapeRec *shape;
	GdkColor *color;
	gint i;

	if (!drawing->xkb)
		return;

	shape = drawing->xkb->geom->shapes + shape_doodad->shape_ndx;
	color = drawing->colors + shape_doodad->color_ndx;

	for (i = 0; i < shape->num_outlines; i++)
		draw_outline (drawing, shape->outlines + i, color,
			      doodad->angle,
			      doodad->origin_x + shape_doodad->left,
			      doodad->origin_y + shape_doodad->top);
}

static void
draw_doodad (GkbdKeyboardDrawing * drawing,
	     GkbdKeyboardDrawingDoodad * doodad)
{
	switch (doodad->doodad->any.type) {
	case XkbOutlineDoodad:
	case XkbSolidDoodad:
		draw_shape_doodad (drawing, doodad,
				   &doodad->doodad->shape);
		break;

	case XkbTextDoodad:
		draw_text_doodad (drawing, doodad, &doodad->doodad->text);
		break;

	case XkbIndicatorDoodad:
		draw_indicator_doodad (drawing, doodad,
				       &doodad->doodad->indicator);
		break;

	case XkbLogoDoodad:
		/* g_print ("draw_doodad: logo: %s\n", doodad->doodad->logo.logo_name); */
		/* XkbLogoDoodadRec is essentially a subclass of XkbShapeDoodadRec */
		draw_shape_doodad (drawing, doodad,
				   &doodad->doodad->shape);
		break;
	}
}

static void
draw_keyboard_item (GkbdKeyboardDrawingItem * item,
		    GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb)
		return;

	switch (item->type) {
	case GKBD_KEYBOARD_DRAWING_ITEM_TYPE_KEY:
		draw_key (drawing, (GkbdKeyboardDrawingKey *) item);
		break;

	case GKBD_KEYBOARD_DRAWING_ITEM_TYPE_DOODAD:
		draw_doodad (drawing, (GkbdKeyboardDrawingDoodad *) item);
		break;
	}
}

static void
draw_keyboard (GkbdKeyboardDrawing * drawing)
{
	GtkStateType state = GTK_WIDGET_STATE (GTK_WIDGET (drawing));
	gint pixw, pixh;

	if (!drawing->xkb)
		return;

	pixw = GTK_WIDGET (drawing)->allocation.width;
	pixh = GTK_WIDGET (drawing)->allocation.height;

	drawing->pixmap =
	    gdk_pixmap_new (GTK_WIDGET (drawing)->window, pixw, pixh, -1);

	/* blank background */
	gdk_draw_rectangle (drawing->pixmap,
			    GTK_WIDGET (drawing)->style->base_gc[state],
			    TRUE, 0, 0, pixw, pixh);

	if (drawing->xkb == NULL)
		return;

#ifdef KBDRAW_DEBUG
	printf ("mods: %d\n", drawing->mods);
#endif

	g_list_foreach (drawing->keyboard_items,
			(GFunc) draw_keyboard_item, drawing);
}

static void
alloc_pango_layout (GkbdKeyboardDrawing * drawing)
{
	PangoContext *context =
	    gtk_widget_get_pango_context (GTK_WIDGET (drawing));
	drawing->layout = pango_layout_new (context);
	pango_layout_set_ellipsize (drawing->layout, PANGO_ELLIPSIZE_END);
}

static void
free_pango_layout (GkbdKeyboardDrawing * drawing)
{
	g_object_unref (G_OBJECT (drawing->layout));
	drawing->layout = NULL;
}

static gboolean
expose_event (GtkWidget * widget,
	      GdkEventExpose * event, GkbdKeyboardDrawing * drawing)
{
	GtkStateType state = GTK_WIDGET_STATE (GTK_WIDGET (drawing));

	if (!drawing->xkb)
		return FALSE;

	if (drawing->pixmap == NULL)
		return FALSE;

	gdk_draw_drawable (widget->window,
			   widget->style->fg_gc[state],
			   drawing->pixmap,
			   event->area.x, event->area.y,
			   event->area.x, event->area.y,
			   event->area.width, event->area.height);

	if (GTK_WIDGET_HAS_FOCUS (widget))
		gtk_paint_focus (widget->style, widget->window,
				 GTK_WIDGET_STATE (widget), &event->area,
				 widget, "keyboard-drawing",
				 0, 0,
				 widget->allocation.width,
				 widget->allocation.height);

	return FALSE;
}

static gboolean
idle_redraw (gpointer user_data)
{
	GkbdKeyboardDrawing *drawing = user_data;

	drawing->idle_redraw = 0;
	draw_keyboard (drawing);
	gtk_widget_queue_draw (GTK_WIDGET (drawing));
	return FALSE;
}

static void
size_allocate (GtkWidget * widget,
	       GtkAllocation * allocation, GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb)
		return;

	if (drawing->pixmap) {
		g_object_unref (drawing->pixmap);
		drawing->pixmap = NULL;
	}

	if (drawing->xkb->geom->width_mm <= 0
	    || drawing->xkb->geom->height_mm <= 0) {
		g_critical
		    ("keyboard geometry reports width or height as zero!");
		return;
	}

	if (allocation->width * drawing->xkb->geom->height_mm <
	    allocation->height * drawing->xkb->geom->width_mm) {
		drawing->scale_numerator = allocation->width;
		drawing->scale_denominator = drawing->xkb->geom->width_mm;
	} else {
		drawing->scale_numerator = allocation->height;
		drawing->scale_denominator = drawing->xkb->geom->height_mm;
	}

	pango_font_description_set_size (drawing->font_desc,
					 36000 * drawing->scale_numerator /
					 drawing->scale_denominator);
	pango_layout_set_spacing (drawing->layout,
				  -8000 * drawing->scale_numerator /
				  drawing->scale_denominator);
	pango_layout_set_font_description (drawing->layout,
					   drawing->font_desc);

	if (!drawing->idle_redraw)
		drawing->idle_redraw = g_idle_add (idle_redraw, drawing);
}

static gint
key_event (GtkWidget * widget,
	   GdkEventKey * event, GkbdKeyboardDrawing * drawing)
{
	GkbdKeyboardDrawingKey *key;
	if (!drawing->xkb)
		return FALSE;

	key = drawing->keys + event->hardware_keycode;

	if (event->hardware_keycode > drawing->xkb->max_key_code ||
	    event->hardware_keycode < drawing->xkb->min_key_code ||
	    key->xkbkey == NULL) {
		g_signal_emit (drawing,
			       gkbd_keyboard_drawing_signals[BAD_KEYCODE],
			       0, event->hardware_keycode);
		return TRUE;
	}

	if ((event->type == GDK_KEY_PRESS && key->pressed) ||
	    (event->type == GDK_KEY_RELEASE && !key->pressed))
		return TRUE;
	/* otherwise this event changes the state we believed we had before */

	key->pressed = (event->type == GDK_KEY_PRESS);

	draw_key (drawing, key);

	invalidate_key_region (drawing, key);

	return TRUE;
}

static gint
button_press_event (GtkWidget * widget,
		    GdkEventButton * event, GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb)
		return FALSE;

	gtk_widget_grab_focus (widget);
	return FALSE;
}

static gboolean
unpress_keys (GkbdKeyboardDrawing * drawing)
{
	gint i;

	if (!drawing->xkb)
		return FALSE;

	for (i = drawing->xkb->min_key_code;
	     i <= drawing->xkb->max_key_code; i++)
		if (drawing->keys[i].pressed) {
			drawing->keys[i].pressed = FALSE;
			draw_key (drawing, drawing->keys + i);
			invalidate_key_region (drawing, drawing->keys + i);
		}

	return FALSE;
}

static gint
focus_event (GtkWidget * widget,
	     GdkEventFocus * event, GkbdKeyboardDrawing * drawing)
{
	if (event->in && drawing->timeout > 0) {
		g_source_remove (drawing->timeout);
		drawing->timeout = 0;
	} else
		drawing->timeout =
		    g_timeout_add (120, (GSourceFunc) unpress_keys,
				   drawing);

	return FALSE;
}

static gint
compare_keyboard_item_priorities (GkbdKeyboardDrawingItem * a,
				  GkbdKeyboardDrawingItem * b)
{
	if (a->priority > b->priority)
		return 1;
	else if (a->priority < b->priority)
		return -1;
	else
		return 0;
}

static void
init_indicator_doodad (GkbdKeyboardDrawing * drawing,
		       XkbDoodadRec * xkbdoodad,
		       GkbdKeyboardDrawingDoodad * doodad)
{
	if (!drawing->xkb)
		return;

	if (xkbdoodad->any.type == XkbIndicatorDoodad) {
		gint index;
		Atom iname = 0;
		Atom sname = xkbdoodad->indicator.name;
		unsigned long phys_indicators =
		    drawing->xkb->indicators->phys_indicators;
		Atom *pind = drawing->xkb->names->indicators;

#ifdef KBDRAW_DEBUG
		printf ("Looking for %d[%s]\n",
			(int) sname, XGetAtomName (drawing->display,
						   sname));
#endif

		for (index = 0; index < XkbNumIndicators; index++) {
			iname = *pind++;
			/* name matches and it is real */
			if (iname == sname
			    && (phys_indicators & (1 << index)))
				break;
			if (iname == 0)
				break;
		}
		if (iname == 0)
			g_warning ("Could not find indicator %d [%s]\n",
				   (int) sname,
				   XGetAtomName (drawing->display, sname));
		else {
#ifdef KBDRAW_DEBUG
			printf ("Found in xkbdesc as %d\n", index);
#endif
			drawing->physical_indicators[index] = doodad;
			/* Trying to obtain the real state, but if fail - just assume OFF */
			if (!XkbGetNamedIndicator
			    (drawing->display, sname, NULL, &doodad->on,
			     NULL, NULL))
				doodad->on = 0;
		}
	}
}

static void
init_keys_and_doodads (GkbdKeyboardDrawing * drawing)
{
	gint i, j, k;
	gint x, y;

	if (!drawing->xkb)
		return;

	for (i = 0; i < drawing->xkb->geom->num_doodads; i++) {
		XkbDoodadRec *xkbdoodad = drawing->xkb->geom->doodads + i;
		GkbdKeyboardDrawingDoodad *doodad =
		    g_new (GkbdKeyboardDrawingDoodad, 1);

		doodad->type = GKBD_KEYBOARD_DRAWING_ITEM_TYPE_DOODAD;
		doodad->origin_x = 0;
		doodad->origin_y = 0;
		doodad->angle = 0;
		doodad->priority = xkbdoodad->any.priority * 256 * 256;
		doodad->doodad = xkbdoodad;

		init_indicator_doodad (drawing, xkbdoodad, doodad);

		drawing->keyboard_items =
		    g_list_append (drawing->keyboard_items, doodad);
	}

	for (i = 0; i < drawing->xkb->geom->num_sections; i++) {
#ifdef KBDRAW_DEBUG
		printf ("initing section %d\n", i);
#endif
		XkbSectionRec *section = drawing->xkb->geom->sections + i;
		guint priority;

		x = section->left;
		y = section->top;
		priority = section->priority * 256 * 256;

		for (j = 0; j < section->num_rows; j++) {
			XkbRowRec *row = section->rows + j;

#ifdef KBDRAW_DEBUG
			printf ("  initing row %d\n", j);
#endif
			x = section->left + row->left;
			y = section->top + row->top;

			for (k = 0; k < row->num_keys; k++) {
				XkbKeyRec *xkbkey = row->keys + k;
				GkbdKeyboardDrawingKey *key;
				XkbShapeRec *shape =
				    drawing->xkb->geom->shapes +
				    xkbkey->shape_ndx;
				guint keycode = find_keycode (drawing,
							      xkbkey->name.
							      name);

#ifdef KBDRAW_DEBUG
				printf
				    ("    initing key %d, shape: %p(%p + %d), code: %d\n",
				     k, shape, drawing->xkb->geom->shapes,
				     xkbkey->shape_ndx, keycode);
#endif
				if (row->vertical)
					y += xkbkey->gap;
				else
					x += xkbkey->gap;

				if (keycode >= drawing->xkb->min_key_code
				    && keycode <=
				    drawing->xkb->max_key_code)
					key = drawing->keys + keycode;
				else {
					g_warning
					    ("key %4.4s: keycode = %u; not in range %d..%d\n",
					     xkbkey->name.name, keycode,
					     drawing->xkb->min_key_code,
					     drawing->xkb->max_key_code);

					key =
					    g_new0 (GkbdKeyboardDrawingKey,
						    1);
				}

				key->type =
				    GKBD_KEYBOARD_DRAWING_ITEM_TYPE_KEY;
				key->xkbkey = xkbkey;
				key->angle = section->angle;
				rotate_coordinate (section->left,
						   section->top, x, y,
						   section->angle,
						   &key->origin_x,
						   &key->origin_y);
				key->priority = priority;
				key->keycode = keycode;

				drawing->keyboard_items =
				    g_list_append (drawing->keyboard_items,
						   key);

				if (row->vertical)
					y += shape->bounds.y2;
				else
					x += shape->bounds.x2;

				priority++;
			}
		}

		for (j = 0; j < section->num_doodads; j++) {
			XkbDoodadRec *xkbdoodad = section->doodads + j;
			GkbdKeyboardDrawingDoodad *doodad =
			    g_new (GkbdKeyboardDrawingDoodad, 1);

			doodad->type =
			    GKBD_KEYBOARD_DRAWING_ITEM_TYPE_DOODAD;
			doodad->origin_x = x;
			doodad->origin_y = y;
			doodad->angle = section->angle;
			doodad->priority =
			    priority + xkbdoodad->any.priority;
			doodad->doodad = xkbdoodad;

			init_indicator_doodad (drawing, xkbdoodad, doodad);

			drawing->keyboard_items =
			    g_list_append (drawing->keyboard_items,
					   doodad);
		}
	}

	drawing->keyboard_items = g_list_sort (drawing->keyboard_items,
					       (GCompareFunc)
					       compare_keyboard_item_priorities);
}

static void
init_colors (GkbdKeyboardDrawing * drawing)
{
	gboolean result;
	gint i;

	if (!drawing->xkb)
		return;

	drawing->colors = g_new (GdkColor, drawing->xkb->geom->num_colors);

	for (i = 0; i < drawing->xkb->geom->num_colors; i++) {
		result =
		    parse_xkb_color_spec (drawing->xkb->geom->colors[i].
					  spec, drawing->colors + i);

		if (!result)
			g_warning
			    ("init_colors: unable to parse color %s\n",
			     drawing->xkb->geom->colors[i].spec);
	}
}

static void
free_cdik (			/*colors doodads indicators keys */
		  GkbdKeyboardDrawing * drawing)
{
	GList *itemp;

	if (!drawing->xkb)
		return;

	for (itemp = drawing->keyboard_items; itemp; itemp = itemp->next) {
		GkbdKeyboardDrawingItem *item = itemp->data;
		GkbdKeyboardDrawingKey *key;

		switch (item->type) {
		case GKBD_KEYBOARD_DRAWING_ITEM_TYPE_DOODAD:
			g_free (item);
			break;

		case GKBD_KEYBOARD_DRAWING_ITEM_TYPE_KEY:
			key = (GkbdKeyboardDrawingKey *) item;
			if (key->keycode < drawing->xkb->min_key_code ||
			    key->keycode > drawing->xkb->max_key_code)
				g_free (key);
			/* otherwise it's part of the array */
			break;
		}
	}

	g_list_free (drawing->keyboard_items);
	drawing->keyboard_items = NULL;

	g_free (drawing->keys);
	g_free (drawing->colors);
}

static void
alloc_cdik (GkbdKeyboardDrawing * drawing)
{
	drawing->physical_indicators_size =
	    drawing->xkb->indicators->phys_indicators + 1;
	drawing->physical_indicators =
	    g_new0 (GkbdKeyboardDrawingDoodad *,
		    drawing->physical_indicators_size);
	drawing->keys =
	    g_new0 (GkbdKeyboardDrawingKey,
		    drawing->xkb->max_key_code + 1);
}

static GdkFilterReturn
xkb_state_notify_event_filter (GdkXEvent * gdkxev,
			       GdkEvent * event,
			       GkbdKeyboardDrawing * drawing)
{
#define group_change_mask (XkbGroupStateMask | XkbGroupBaseMask | XkbGroupLatchMask | XkbGroupLockMask)
#define modifier_change_mask (XkbModifierStateMask | XkbModifierBaseMask | XkbModifierLatchMask | XkbModifierLockMask)

	if (!drawing->xkb)
		return GDK_FILTER_CONTINUE;

	if (((XEvent *) gdkxev)->type == drawing->xkb_event_type) {
		XkbEvent *kev = (XkbEvent *) gdkxev;
		switch (kev->any.xkb_type) {
		case XkbStateNotify:
			if (((kev->state.changed & modifier_change_mask) &&
			     drawing->track_modifiers)) {
				free_cdik (drawing);
				if (drawing->track_modifiers)
					gkbd_keyboard_drawing_set_mods
					    (drawing,
					     kev->state.compat_state);
				drawing->keys =
				    g_new0 (GkbdKeyboardDrawingKey,
					    drawing->xkb->max_key_code +
					    1);
				size_allocate (GTK_WIDGET (drawing),
					       &(GTK_WIDGET (drawing)->
						 allocation), drawing);

				init_keys_and_doodads (drawing);
				init_colors (drawing);
			}
			break;

		case XkbIndicatorStateNotify:
			{
				/* Good question: should we track indicators when the keyboard is
				   NOT really taken from the screen */
				XkbIndicatorNotifyEvent *iev =
				    &((XkbEvent *) gdkxev)->indicators;
				gint i;

				for (i = 0;
				     i <=
				     drawing->xkb->indicators->
				     phys_indicators; i++)
					if (drawing->
					    physical_indicators[i] != NULL
					    && (iev->changed & 1 << i)) {
						gint state =
						    (iev->
						     state & 1 << i) !=
						    FALSE;

						if ((state
						     && !drawing->
						     physical_indicators
						     [i]->on) || (!state
								  &&
								  drawing->
								  physical_indicators
								  [i]->
								  on)) {
							drawing->
							    physical_indicators
							    [i]->on =
							    state;
							draw_doodad
							    (drawing,
							     drawing->
							     physical_indicators
							     [i]);
							invalidate_indicator_doodad_region
							    (drawing,
							     drawing->
							     physical_indicators
							     [i]);
						}
					}
			}
			break;

		case XkbIndicatorMapNotify:
		case XkbControlsNotify:
		case XkbNamesNotify:
		case XkbNewKeyboardNotify:
			{
				XkbStateRec state;
				memset (&state, 0, sizeof (state));
				XkbGetState (drawing->display,
					     XkbUseCoreKbd, &state);
				if (drawing->track_modifiers)
					gkbd_keyboard_drawing_set_mods
					    (drawing, state.compat_state);
				if (drawing->track_config)
					gkbd_keyboard_drawing_set_keyboard
					    (drawing, NULL);
			}
			break;
		}
	}

	return GDK_FILTER_CONTINUE;
}

static void
destroy (GkbdKeyboardDrawing * drawing)
{
	free_pango_layout (drawing);
	gdk_window_remove_filter (NULL, (GdkFilterFunc)
				  xkb_state_notify_event_filter, drawing);
	if (drawing->timeout > 0) {
		g_source_remove (drawing->timeout);
		drawing->timeout = 0;
	}
	if (drawing->idle_redraw > 0) {
		g_source_remove (drawing->idle_redraw);
		drawing->idle_redraw = 0;
	}

	g_object_unref (drawing->pixmap);
}

static void
style_changed (GkbdKeyboardDrawing * drawing)
{
	pango_layout_context_changed (drawing->layout);
}

static void
gkbd_keyboard_drawing_init (GkbdKeyboardDrawing * drawing)
{
	gint opcode = 0, error = 0, major = 1, minor = 0;
	gint mask;

	drawing->display =
	    GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
	printf ("dpy: %p\n", drawing->display);

	if (!XkbQueryExtension
	    (drawing->display, &opcode, &drawing->xkb_event_type, &error,
	     &major, &minor))
		g_critical
		    ("XkbQueryExtension failed! Stuff probably won't work.");

	printf ("evt/error/major/minor: %d/%d/%d/%d\n",
		drawing->xkb_event_type, error, major, minor);

	/* XXX: this stuff probably doesn't matter.. also, gdk_screen_get_default can fail */
	if (gtk_widget_has_screen (GTK_WIDGET (drawing)))
		drawing->screen_num =
		    gdk_screen_get_number (gtk_widget_get_screen
					   (GTK_WIDGET (drawing)));
	else
		drawing->screen_num =
		    gdk_screen_get_number (gdk_screen_get_default ());

	drawing->pixmap = NULL;
	alloc_pango_layout (drawing);

	drawing->font_desc =
	    pango_font_description_copy (GTK_WIDGET (drawing)->style->
					 font_desc);
	drawing->keyboard_items = NULL;
	drawing->colors = NULL;
	drawing->angle = 0;
	drawing->scale_numerator = 1;
	drawing->scale_denominator = 1;

	drawing->track_modifiers = 0;
	drawing->track_config = 0;

	gtk_widget_set_double_buffered (GTK_WIDGET (drawing), FALSE);

	/* XXX: XkbClientMapMask | XkbIndicatorMapMask | XkbNamesMask | XkbGeometryMask */
	drawing->xkb = XkbGetKeyboard (drawing->display,
				       XkbGBN_GeometryMask |
				       XkbGBN_KeyNamesMask |
				       XkbGBN_OtherNamesMask |
				       XkbGBN_SymbolsMask |
				       XkbGBN_IndicatorMapMask,
				       XkbUseCoreKbd);
	if (drawing->xkb == NULL) {
		g_critical
		    ("XkbGetKeyboard failed to get keyboard from the server!");
		return;
	}

	XkbGetNames (drawing->display, XkbAllNamesMask, drawing->xkb);
	drawing->l3mod = XkbKeysymToModifiers (drawing->display,
					       GDK_ISO_Level3_Shift);

	drawing->xkbOnDisplay = TRUE;

	alloc_cdik (drawing);

	XkbSelectEventDetails (drawing->display, XkbUseCoreKbd,
			       XkbIndicatorStateNotify,
			       drawing->xkb->indicators->phys_indicators,
			       drawing->xkb->indicators->phys_indicators);

	mask =
	    (XkbStateNotifyMask | XkbNamesNotifyMask |
	     XkbControlsNotifyMask | XkbIndicatorMapNotifyMask |
	     XkbNewKeyboardNotifyMask);
	XkbSelectEvents (drawing->display, XkbUseCoreKbd, mask, mask);

	mask = XkbGroupStateMask | XkbModifierStateMask;
	XkbSelectEventDetails (drawing->display, XkbUseCoreKbd,
			       XkbStateNotify, mask, mask);

	mask = (XkbGroupNamesMask | XkbIndicatorNamesMask);
	XkbSelectEventDetails (drawing->display, XkbUseCoreKbd,
			       XkbNamesNotify, mask, mask);
	init_keys_and_doodads (drawing);
	init_colors (drawing);

	/* required to get key events */
	GTK_WIDGET_SET_FLAGS (GTK_WIDGET (drawing), GTK_CAN_FOCUS);

	gtk_widget_set_events (GTK_WIDGET (drawing),
			       GDK_EXPOSURE_MASK | GDK_KEY_PRESS_MASK |
			       GDK_KEY_RELEASE_MASK | GDK_BUTTON_PRESS_MASK
			       | GDK_FOCUS_CHANGE_MASK);
	g_signal_connect (G_OBJECT (drawing), "expose-event",
			  G_CALLBACK (expose_event), drawing);
	g_signal_connect (G_OBJECT (drawing), "key-press-event",
			  G_CALLBACK (key_event), drawing);
	g_signal_connect (G_OBJECT (drawing), "key-release-event",
			  G_CALLBACK (key_event), drawing);
	g_signal_connect (G_OBJECT (drawing), "button-press-event",
			  G_CALLBACK (button_press_event), drawing);
	g_signal_connect (G_OBJECT (drawing), "focus-out-event",
			  G_CALLBACK (focus_event), drawing);
	g_signal_connect (G_OBJECT (drawing), "focus-in-event",
			  G_CALLBACK (focus_event), drawing);
	g_signal_connect (G_OBJECT (drawing), "size-allocate",
			  G_CALLBACK (size_allocate), drawing);
	g_signal_connect (G_OBJECT (drawing), "destroy",
			  G_CALLBACK (destroy), drawing);
	g_signal_connect (G_OBJECT (drawing), "style-set",
			  G_CALLBACK (style_changed), drawing);

	gdk_window_add_filter (NULL, (GdkFilterFunc)
			       xkb_state_notify_event_filter, drawing);
}

GtkWidget *
gkbd_keyboard_drawing_new (void)
{
	return
	    GTK_WIDGET (g_object_new
			(gkbd_keyboard_drawing_get_type (), NULL));
}

static void
gkbd_keyboard_drawing_class_init (GkbdKeyboardDrawingClass * klass)
{
	klass->bad_keycode = NULL;

	gkbd_keyboard_drawing_signals[BAD_KEYCODE] =
	    g_signal_new ("bad-keycode", gkbd_keyboard_drawing_get_type (),
			  G_SIGNAL_RUN_FIRST,
			  G_STRUCT_OFFSET (GkbdKeyboardDrawingClass,
					   bad_keycode), NULL, NULL,
			  gkbd_keyboard_drawing_VOID__UINT, G_TYPE_NONE, 1,
			  G_TYPE_UINT);
}

GType
gkbd_keyboard_drawing_get_type (void)
{
	static GType gkbd_keyboard_drawing_type = 0;

	if (!gkbd_keyboard_drawing_type) {
		static const GTypeInfo gkbd_keyboard_drawing_info = {
			sizeof (GkbdKeyboardDrawingClass),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc) gkbd_keyboard_drawing_class_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			sizeof (GkbdKeyboardDrawing),
			0,	/* n_preallocs */
			(GInstanceInitFunc) gkbd_keyboard_drawing_init,
		};

		gkbd_keyboard_drawing_type =
		    g_type_register_static (GTK_TYPE_DRAWING_AREA,
					    "GkbdKeyboardDrawing",
					    &gkbd_keyboard_drawing_info,
					    0);
	}

	return gkbd_keyboard_drawing_type;
}

void
gkbd_keyboard_drawing_set_mods (GkbdKeyboardDrawing * drawing, guint mods)
{
#ifdef KBDRAW_DEBUG
	printf ("set_mods: %d\n", mods);
#endif
	if (mods != drawing->mods) {
		drawing->mods = mods;
		gtk_widget_queue_draw (GTK_WIDGET (drawing));
	}
}

/* returns a pixbuf with the keyboard drawing at the current pixel size
 * (which can then be saved to disk, etc) */
GdkPixbuf *
gkbd_keyboard_drawing_get_pixbuf (GkbdKeyboardDrawing * drawing)
{
	if (drawing->pixmap == NULL)
		draw_keyboard (drawing);

	return gdk_pixbuf_get_from_drawable (NULL, drawing->pixmap, NULL,
					     0, 0, 0, 0,
					     xkb_to_pixmap_coord (drawing,
								  drawing->
								  xkb->
								  geom->
								  width_mm),
					     xkb_to_pixmap_coord (drawing,
								  drawing->
								  xkb->
								  geom->
								  height_mm));
}

gboolean
gkbd_keyboard_drawing_set_keyboard (GkbdKeyboardDrawing * drawing,
				    XkbComponentNamesRec * names)
{
	free_cdik (drawing);
	if (drawing->xkb)
		XkbFreeKeyboard (drawing->xkb, 0, TRUE);	/* free_all = TRUE */
	drawing->xkb = NULL;

	if (names) {
		drawing->xkb =
		    XkbGetKeyboardByName (drawing->display, XkbUseCoreKbd,
					  names, 0,
					  XkbGBN_GeometryMask |
					  XkbGBN_KeyNamesMask |
					  XkbGBN_OtherNamesMask |
					  XkbGBN_ClientSymbolsMask |
					  XkbGBN_IndicatorMapMask, FALSE);
		drawing->xkbOnDisplay = FALSE;
	} else {
		drawing->xkb = XkbGetKeyboard (drawing->display,
					       XkbGBN_GeometryMask |
					       XkbGBN_KeyNamesMask |
					       XkbGBN_OtherNamesMask |
					       XkbGBN_SymbolsMask |
					       XkbGBN_IndicatorMapMask,
					       XkbUseCoreKbd);
		XkbGetNames (drawing->display, XkbAllNamesMask,
			     drawing->xkb);
		drawing->xkbOnDisplay = TRUE;
	}

	if (drawing->xkb == NULL)
		return FALSE;

	alloc_cdik (drawing);

	init_keys_and_doodads (drawing);
	init_colors (drawing);

	size_allocate (GTK_WIDGET (drawing),
		       &(GTK_WIDGET (drawing)->allocation), drawing);
	gtk_widget_queue_draw (GTK_WIDGET (drawing));

	return TRUE;
}

G_CONST_RETURN gchar *
gkbd_keyboard_drawing_get_keycodes (GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb || drawing->xkb->names->keycodes <= 0)
		return NULL;
	else
		return XGetAtomName (drawing->display,
				     drawing->xkb->names->keycodes);
}

G_CONST_RETURN gchar *
gkbd_keyboard_drawing_get_geometry (GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb || drawing->xkb->names->geometry <= 0)
		return NULL;
	else
		return XGetAtomName (drawing->display,
				     drawing->xkb->names->geometry);
}

G_CONST_RETURN gchar *
gkbd_keyboard_drawing_get_symbols (GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb || drawing->xkb->names->symbols <= 0)
		return NULL;
	else
		return XGetAtomName (drawing->display,
				     drawing->xkb->names->symbols);
}

G_CONST_RETURN gchar *
gkbd_keyboard_drawing_get_types (GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb || drawing->xkb->names->types <= 0)
		return NULL;
	else
		return XGetAtomName (drawing->display,
				     drawing->xkb->names->types);
}

G_CONST_RETURN gchar *
gkbd_keyboard_drawing_get_compat (GkbdKeyboardDrawing * drawing)
{
	if (!drawing->xkb || drawing->xkb->names->compat <= 0)
		return NULL;
	else
		return XGetAtomName (drawing->display,
				     drawing->xkb->names->compat);
}

void
gkbd_keyboard_drawing_set_track_modifiers (GkbdKeyboardDrawing * drawing,
					   gboolean enable)
{
	if (enable) {
		XkbStateRec state;
		drawing->track_modifiers = 1;
		memset (&state, 0, sizeof (state));
		XkbGetState (drawing->display, XkbUseCoreKbd, &state);
		gkbd_keyboard_drawing_set_mods (drawing,
						state.compat_state);
	} else
		drawing->track_modifiers = 0;
}

void
gkbd_keyboard_drawing_set_track_config (GkbdKeyboardDrawing * drawing,
					gboolean enable)
{
	if (enable)
		drawing->track_config = 1;
	else
		drawing->track_config = 0;
}

void
gkbd_keyboard_drawing_set_groups_levels (GkbdKeyboardDrawing * drawing,
					 GkbdKeyboardDrawingGroupLevel *
					 groupLevels[])
{
#ifdef KBDRAW_DEBUG
	printf ("set_group_levels [topLeft]: %d %d \n",
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_TOPLEFT]->group,
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_TOPLEFT]->level);
	printf ("set_group_levels [topRight]: %d %d \n",
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_TOPRIGHT]->group,
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_TOPRIGHT]->level);
	printf ("set_group_levels [bottomLeft]: %d %d \n",
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_BOTTOMLEFT]->group,
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_BOTTOMLEFT]->level);
	printf ("set_group_levels [bottomRight]: %d %d \n",
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_BOTTOMRIGHT]->group,
		groupLevels[GKBD_KEYBOARD_DRAWING_POS_BOTTOMRIGHT]->level);
#endif
	drawing->groupLevels = groupLevels;

	gtk_widget_queue_draw (GTK_WIDGET (drawing));
}
