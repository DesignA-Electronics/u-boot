
#include <common.h>
#include <command.h>
#include <malloc.h>
#include <spi_flash.h>
#include <errno.h>
#include <search.h>
#include <env_internal.h>
#include <asm/arch-imx/cpu.h>

#define ACTIVESET_FLASH_LEN (0x10000)

static u32 activeset_env_addrs[] = {
	0xD0000,
	0xE0000,
};

struct hsearch_data activeset_htab;
static struct spi_flash *activeset_flash;

// spi_flash_free(activeset_flash);
// activeset_flash = NULL;

static int init_flash(void)
{
	if (activeset_flash)
		return 0;

	activeset_flash = spi_flash_probe(CONFIG_ENV_SPI_BUS,
			CONFIG_ENV_SPI_CS,
			CONFIG_ENV_SPI_MAX_HZ, CONFIG_ENV_SPI_MODE);
	if (!activeset_flash) {
		printf("error: spi flash probe failed\n");
		return -1;
	}

	return 0;
}

#if 0
static void print_set_env(void)
{
	char *res = NULL;
	int len;

	len = hexport_r(&activeset_htab, '\n', 0, &res, 0, 0, NULL);
	if (len > 0) {
		puts(res);
		free(res);
	}
}
#endif

static int load_set_env(int idx)
{
	char *buf;
	int ret;

	if (init_flash() < 0)
		return -1;

	buf = (char *)malloc(ACTIVESET_FLASH_LEN);

	ret = spi_flash_read(activeset_flash, activeset_env_addrs[idx], ACTIVESET_FLASH_LEN, buf);
	if (ret < 0) {
		printf("error: set environment spi flash read failed\n");
		ret = -1;
		goto out;
	}

	if (env_import(buf, 1) != 0) {
		printf("error: set environment import failed\n");
		ret = -1;
		goto out;
	}

	ret = 0;
 out:
	free(buf);
	return ret;
}

static int save_set_env(int idx)
{
	env_t	env_new;
	int ret;

	if (init_flash() < 0)
		return -1;

	ret = env_export(&env_new);
	if (ret < 0)
		return ret;

	ret = spi_flash_erase(activeset_flash, activeset_env_addrs[idx], ACTIVESET_FLASH_LEN);
	if (ret < 0) {
		printf("error: set environment export spi flash erase failed\n");
		return ret;
	}

	ret = spi_flash_write(activeset_flash, activeset_env_addrs[idx], ACTIVESET_FLASH_LEN, (uchar *)&env_new);
	if (ret < 0) {
		printf("error: set environment export spi flash write failed\n");
		return ret;
	}

	return 0;
}

static int get_activeset_idx(void)
{
	struct env_entry e = {}, *ep;
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(activeset_env_addrs); i++) {

		ret = load_set_env(i);
		if (ret < 0)
			return -1;

		e.key = "activeset";
		e.data = NULL;
		hsearch_r(e, ENV_FIND, &ep, &activeset_htab, 0);
		if (ep == NULL)
			return -1;

		if (strcmp(ep->data, "1") == 0)
			break;

	}

	if (i >= ARRAY_SIZE(activeset_env_addrs))
		return -1;

	return i;
}

static int print_activeset(void)
{
	int idx;

	idx = get_activeset_idx();

	if (idx < 0)
		printf("active set idx: unknown\n");
	else
		printf("active set idx: %d\n", idx);

	return CMD_RET_SUCCESS;
}

static int update_set_active_state(int idx, int active)
{
	char value[2];
	struct env_entry e = {}, *ep;
	int ret;

	ret = load_set_env(idx);
	if (ret < 0)
		return -1;

	value[0] = active ? '1' : '0';
	value[1] = '\0';

	e.key	= "activeset";
	e.data	= value;
	hsearch_r(e, ENV_ENTER, &ep, &activeset_htab, 0);
	if (!ep)
		return -1;

	//print_set_env();

	ret = save_set_env(idx);
	if (ret < 0)
		return -1;

	return 0;
}

static int set_activeset(int idx)
{
	int ret;
	int i;

	if (idx < 0 || idx > ARRAY_SIZE(activeset_env_addrs)) {
		printf("invalid set index: %d\n", idx);
		return CMD_RET_FAILURE;
	}

	ret = update_set_active_state(idx, 1);
	if (ret < 0)
		return CMD_RET_FAILURE;

	for (i = 0; i < ARRAY_SIZE(activeset_env_addrs); i++) {
		if (i == idx)
			continue;
		ret = update_set_active_state(i, 0);
		if (ret < 0)
			return CMD_RET_FAILURE;
	}


	return CMD_RET_SUCCESS;
}

static int increment_activeset_after_watchdog_reset(void)
{
	int idx;
	int ret;

	if (get_imx_reset_cause() != 0x00010) {
		//printf("CPU reset cause was not watchdog\n");
		return CMD_RET_SUCCESS;
	}

	idx = get_activeset_idx();
	if (idx < 0) {
		printf("error: failed to get current index\n");
		return CMD_RET_FAILURE;
	}

	idx++;
	if (idx >= ARRAY_SIZE(activeset_env_addrs))
		idx = 0;

	ret = set_activeset(idx);
	if (ret == CMD_RET_SUCCESS)
		printf("active set idx set to: %d\n", idx);

	return ret;
}

static int do_activeset(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	if (argc == 1)
		return print_activeset();

	if (argc == 2) {

		if (strcmp(argv[1], "-w") == 0)
			return increment_activeset_after_watchdog_reset();

		return set_activeset(simple_strtoul(argv[1], NULL, 10));
	}

	return CMD_RET_FAILURE;
}

U_BOOT_CMD(
	activeset,	2,		0,	do_activeset,
	"print or update the active image set index",
	"[-w | index]\n"
	"If no option is specified the current active index will be displayed.\n"
	"If -w is specified the active index will be incremented if the CPU reset\n"
	"cause was the watchdog triggering\n"
	"If a number is specified the current active index will be set to that."
);
