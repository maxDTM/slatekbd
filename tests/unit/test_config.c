/* test_config: defaults, parsing, junk, unknown keys, round trip, CLI-override preservation. */
#include <string.h>
#include <unistd.h>

#include "config.h"
#include "t.h"

int main(void)
{
	config_verbose = 0;
	struct config c, d;
	config_defaults(&c);
	CHECK(c.mode == MODE_POPUP && c.auto_show && c.height_land == 280 && c.height_port == 340);
	CHECK(c.repeat_delay == 400 && c.repeat_rate == 25 && c.split_gap == 30 && !strcmp(c.output, "auto"));

	config_parse_buffer(&c,
		"# comment\n"
		"mode = overlay\n"
		"  auto=off  \n"
		"font_scale = 1.4   # trailing comment\n"
		"key_shape = square\n"
		"key_radius = 99\n"           /* out of range -> default kept */
		"split = yes\n"
		"height_landscape = 300\n"
		"theme = light\n"
		"modifiers = hold\n"
		"rotate_cmd = echo %o %t > /tmp/x\n"
		"bogus_key = 12\n"
		"garbage line without equals\n"
		"lockscreen = always\n"
		"output = DP-3\n");
	CHECK(c.mode == MODE_OVERLAY);
	CHECK(!c.auto_show);
	CHECK(c.font_scale > 1.39 && c.font_scale < 1.41);
	CHECK(c.shape == SHAPE_SQUARE);
	CHECK(c.radius == 6);
	CHECK(c.split);
	CHECK(c.height_land == 300);
	CHECK(c.theme == THEME_LIGHT);
	CHECK(c.modmode == MODMODE_HOLD);
	CHECK(!strcmp(c.rotate_cmd, "echo %o %t > /tmp/x"));
	CHECK(c.lockscreen == LOCK_ALWAYS);
	CHECK(!strcmp(c.output, "DP-3"));
	CHECK(c.nextra == 1 && strstr(c.extra[0], "bogus_key"));

	/* round trip */
	char buf[16384];
	int n = config_serialize(&c, buf, sizeof buf);
	CHECK(n > 0);
	config_defaults(&d);
	config_parse_buffer(&d, buf);
	CHECK(d.mode == c.mode && d.auto_show == c.auto_show && d.shape == c.shape && d.split == c.split);
	CHECK(d.height_land == 300 && d.theme == THEME_LIGHT && d.modmode == MODMODE_HOLD);
	CHECK(!strcmp(d.rotate_cmd, c.rotate_cmd) && !strcmp(d.output, "DP-3"));
	CHECK(d.font_scale > 1.39 && d.font_scale < 1.41);
	CHECK(d.nextra == 1);
	config_free(&d);
	config_free(&c);

	/* CLI override keeps the file value on save */
	config_defaults(&c);
	c.overridden |= 1ULL << CK_HEIGHT_LAND;
	config_parse_buffer(&c, "height_landscape = 320\ntheme = light\n");
	CHECK(c.height_land == 280); /* file value not applied over the CLI */
	CHECK(config_set(&c, "height_landscape", "450") == 0);
	CHECK(c.height_land == 450);
	n = config_serialize(&c, buf, sizeof buf);
	CHECK(strstr(buf, "height_landscape = 320") != NULL);
	CHECK(strstr(buf, "450") == NULL);
	config_free(&c);

	/* bad values */
	config_defaults(&c);
	CHECK(config_set(&c, "mode", "sideways") == 2);
	CHECK(config_set(&c, "repeat_rate", "abc") == 2);
	CHECK(config_set(&c, "nope", "1") == 1);
	CHECK(config_set(&c, "height", "200") == 0 && c.height_land == 200);
	CHECK(config_set(&c, "font_scale", "nan") == 2);
	CHECK(config_set(&c, "font_scale", "inf") == 2);
	CHECK(c.font_scale > 0.99 && c.font_scale < 1.01);
	{
		struct config e;
		config_defaults(&e);
		e.font_scale = 0.0 / 0.0;
		config_clamp(&e);
		CHECK(e.font_scale > 0.99 && e.font_scale < 1.01);
		/* --shown is a CLI override of `start`: the file's value survives a save */
		e.overridden |= 1ULL << CK_START;
		config_parse_buffer(&e, "start = hidden\n");
		CHECK(config_set(&e, "start", "shown") == 0 && e.start == START_SHOWN);
		char sb[8192];
		CHECK(config_serialize(&e, sb, sizeof sb) > 0);
		CHECK(strstr(sb, "= shown") == NULL && strstr(sb, "= hidden") != NULL);
		config_free(&e);
	}

	/* atomic save and reload */
	char path[] = "/tmp/slatekbd-test-XXXXXX";
	char *dir = mkdtemp(path);
	CHECK(dir != NULL);
	if (dir) {
		char file[256];
		snprintf(file, sizeof file, "%s/sub/config", dir);
		c.theme = THEME_LIGHT;
		CHECK(config_save(&c, file) == 0);
		config_defaults(&d);
		CHECK(config_load(&d, file) == 0);
		CHECK(d.theme == THEME_LIGHT && d.height_land == 200);
		config_free(&d);
		unlink(file);
		char sub[256];
		snprintf(sub, sizeof sub, "%s/sub", dir);
		rmdir(sub);
		rmdir(dir);
	}
	config_free(&c);
	T_DONE();
}
