/*
 * sound/soc/md800.c
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *
 * History:
 *	2011/10/19 - [Cao Rongrong] Created file
 *	2013/01/18 - [Ken He]	Port to 3.8
 *
 * Copyright (C) 2004-2009, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include <plat/audio.h>

#include "ambarella_i2s.h"
#include "../codecs/es8328.h"

/* Headset jack detection DAPM pins */
static struct snd_soc_jack hp_jack;

static struct snd_soc_jack_pin hp_jack_pins[] = {
	{
		.pin = "Speaker",
		.mask = SND_JACK_HEADPHONE,
		.invert = 1,
	},
};

static struct snd_soc_jack_gpio hp_jack_gpios[] = {
	{
		.gpio = GPIO(12),
		.name = "hpdet-gpio",
		.report = SND_JACK_HEADPHONE,
		.debounce_time = 200,
		.invert = 1,
	},
};

#define MD800_SPK_ON    0
#define MD800_SPK_OFF   1

/* default: Speaker output */
static int md800_spk_func = MD800_SPK_ON;
static int md800_spk_gpio = GPIO(139);
static int md800_spk_level = GPIO_HIGH;

static struct notifier_block hp_event;

/* headphone inserted, so we disable speaker. */
static int md800_hp_event(struct notifier_block *nb, unsigned long val, void *data)
{
	if (val != 0)
		md800_spk_func = MD800_SPK_OFF;
	else
		md800_spk_func = MD800_SPK_ON;

	return NOTIFY_OK;
}

static void md800_ext_control(struct snd_soc_codec *codec)
{
	int errorCode = 0;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	if (md800_spk_func == MD800_SPK_ON){
		errorCode = gpio_direction_output(md800_spk_gpio, !!md800_spk_level);
		if (errorCode < 0) {
			printk(KERN_ERR "Could not Set Spk-Ctrl GPIO high\n");
			goto err_exit;
		}
		mdelay(1);
		snd_soc_dapm_enable_pin(dapm, "Speaker");
	} else {
		errorCode = gpio_direction_output(md800_spk_gpio, !md800_spk_level);
		if (errorCode < 0) {
			printk(KERN_ERR "Could not Set Spk-Ctrl GPIO low\n");
			goto err_exit;
		}
		mdelay(1);
		snd_soc_dapm_disable_pin(dapm, "Speaker");
	}

	/* signal a DAPM event */
	snd_soc_dapm_sync(dapm);

err_exit:
	return;
}

static int md800_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;

	/* check the jack status at stream startup */
	md800_ext_control(codec);
	return 0;
}

static int md800_board_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int errorCode = 0, mclk, oversample;

	switch (params_rate(params)) {
	case 8000:
		mclk = 2048000;
		oversample = AudioCodec_256xfs;
		break;
	case 11025:
		mclk = 2822400;
		oversample = AudioCodec_256xfs;
		break;
	case 12000:
		mclk = 3072000;
		oversample = AudioCodec_256xfs;
		break;
	case 16000:
		mclk = 4096000;
		oversample = AudioCodec_256xfs;
		break;
	case 22050:
		mclk = 5644800;
		oversample = AudioCodec_256xfs;
		break;
	case 24000:
		mclk = 6144000;
		oversample = AudioCodec_256xfs;
		break;
	case 32000:
		mclk = 8192000;
		oversample = AudioCodec_256xfs;
		break;
	case 44100:
		mclk = 11289600;
		oversample = AudioCodec_256xfs;
		break;
	case 48000:
		mclk = 12288000;
		oversample = AudioCodec_256xfs;
		break;
	default:
		errorCode = -EINVAL;
		goto hw_params_exit;
	}

	/* set the I2S system data format*/
	errorCode = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (errorCode < 0) {
		pr_err("can't set codec DAI configuration\n");
		goto hw_params_exit;
	}

	errorCode = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (errorCode < 0) {
		pr_err("can't set cpu DAI configuration\n");
		goto hw_params_exit;
	}

	/* set the I2S system clock*/
	errorCode = snd_soc_dai_set_sysclk(codec_dai, ES8328_SYSCLK, mclk, 0);
	if (errorCode < 0) {
		pr_err("can't set cpu MCLK configuration\n");
		goto hw_params_exit;
	}

	errorCode = snd_soc_dai_set_sysclk(cpu_dai, AMBARELLA_CLKSRC_ONCHIP, mclk, 0);
	if (errorCode < 0) {
		pr_err("can't set cpu MCLK configuration\n");
		goto hw_params_exit;
	}

	errorCode = snd_soc_dai_set_clkdiv(cpu_dai, AMBARELLA_CLKDIV_LRCLK, oversample);
	if (errorCode < 0) {
		pr_err("can't set cpu MCLK/SF ratio\n");
		goto hw_params_exit;
	}

hw_params_exit:
	return errorCode;
}



static struct snd_soc_ops md800_board_ops = {
	.startup = md800_startup,
	.hw_params = md800_board_hw_params,
};

static int md800_get_spk(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = md800_spk_func;
	return 0;
}

static int md800_set_spk(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_codec *codec =  snd_kcontrol_chip(kcontrol);

	if (md800_spk_func == ucontrol->value.integer.value[0])
		return 0;

	md800_spk_func = ucontrol->value.integer.value[0];
	md800_ext_control(codec);
	return 1;
}

static int md800_spk_event(struct snd_soc_dapm_widget *w,
		struct snd_kcontrol *k, int event)
{
	int errorCode = 0;

	if (SND_SOC_DAPM_EVENT_ON(event) && (md800_spk_func == MD800_SPK_ON)){
		errorCode = gpio_direction_output(md800_spk_gpio, !!md800_spk_level);
		if (errorCode < 0) {
			printk(KERN_ERR "Could not Set Spk-Ctrl GPIO high\n");
			goto err_exit;
		}
	} else {
		errorCode = gpio_direction_output(md800_spk_gpio, !md800_spk_level);
		if (errorCode < 0) {
			printk(KERN_ERR "Could not Set Spk-Ctrl GPIO low\n");
			goto err_exit;
		}
	}
	mdelay(1);

err_exit:
	return errorCode;
}


/* md800 dapm widgets */
static const struct snd_soc_dapm_widget md800_dapm_widgets[] = {
    SND_SOC_DAPM_MIC("Mic Jack", NULL),
    SND_SOC_DAPM_HP("Headphone Jack", NULL),
    SND_SOC_DAPM_SPK("Speaker", md800_spk_event),
};

/* md800 audio map (connections to the codec pins) */
static const struct snd_soc_dapm_route md800_audio_map[] = {
	/* mic is connected to MICIN (via right channel of headphone jack) */
	{"MICIN", NULL, "Mic Jack"},

	/* headphone connected to LHPOUT1, RHPOUT1 */
	{"Headphone Jack", NULL, "LOUT1"},
	{"Headphone Jack", NULL, "ROUT1"},

	/* speaker connected to LOUT, ROUT */
	{"Speaker", NULL, "ROUT1"},
	{"Speaker", NULL, "LOUT1"},
};

static const char *spk_function[]  = {"On", "Off"};

static const struct soc_enum md800_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(spk_function), spk_function),
};

static const struct snd_kcontrol_new es8328_md800_controls[] = {
	SOC_ENUM_EXT("Speaker Function", md800_enum[0], md800_get_spk , md800_set_spk),
};

static int md800_es8328_init(struct snd_soc_pcm_runtime *rtd)
{
	int errorCode = 0;
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* not connected */
	snd_soc_dapm_nc_pin(dapm, "LINPUT1");
	snd_soc_dapm_nc_pin(dapm, "RINPUT1");
	snd_soc_dapm_nc_pin(dapm, "LOUT2");
	snd_soc_dapm_nc_pin(dapm, "ROUT2");

	snd_soc_dapm_enable_pin(dapm, "MICIN");

	/* Add md800 specific controls */
	snd_soc_add_codec_controls(codec, es8328_md800_controls,
			ARRAY_SIZE(es8328_md800_controls));

	/* Add md800 specific widgets */
	snd_soc_dapm_new_controls(dapm,
			md800_dapm_widgets,
			ARRAY_SIZE(md800_dapm_widgets));

	/* Setup md800 specific audio path md800_audio_map */
	snd_soc_dapm_add_routes(dapm,
			md800_audio_map,
			ARRAY_SIZE(md800_audio_map));

	ambarella_i2s_add_controls(codec);

	/* Headset jack detection */
	errorCode = snd_soc_jack_new(codec, "Headset Jack",
				SND_JACK_HEADSET, &hp_jack);
	if (errorCode)
		return errorCode;

	errorCode = snd_soc_jack_add_pins(&hp_jack, ARRAY_SIZE(hp_jack_pins),
				hp_jack_pins);
	if (errorCode)
		return errorCode;

	errorCode = snd_soc_jack_add_gpios(&hp_jack, ARRAY_SIZE(hp_jack_gpios),
				hp_jack_gpios);

	hp_event.notifier_call = md800_hp_event;
	snd_soc_jack_notifier_register(&hp_jack, &hp_event);

	return errorCode;
}


/* md800 digital audio interface glue - connects codec <--> A2S */
static struct snd_soc_dai_link md800_dai_link = {
	.name = "ES8328",
	.stream_name = "ES8328-STREAM",
	.cpu_dai_name = "ambarella-i2s.0",
	.platform_name = "ambarella-pcm-audio",
	.codec_dai_name = "ES8328",
	.codec_name = "es8328-codec.0-0010",
	.init = md800_es8328_init,
	.ops = &md800_board_ops,
};


/* md800 audio machine driver */
static struct snd_soc_card snd_soc_card_md800 = {
	.name = "MD800",
	.owner = THIS_MODULE,
	.dai_link = &md800_dai_link,
	.num_links = 1,
};

static int md800_soc_snd_probe(struct platform_device *pdev)
{
	int errorCode = 0;

	errorCode = gpio_request(md800_spk_gpio, "Spk-Ctrl");
	if (errorCode < 0) {
		printk(KERN_ERR "Could not get Spk-Ctrl GPIO %d\n", md800_spk_gpio);
		goto md800_board_probe_exit1;
	}

	snd_soc_card_md800.dev = &pdev->dev;
	errorCode = snd_soc_register_card(&snd_soc_card_md800);
	if (errorCode) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", errorCode);
		goto md800_board_probe_exit0;
	}

	return 0;

md800_board_probe_exit0:
	gpio_free(md800_spk_gpio);
md800_board_probe_exit1:
	return errorCode;
}

static int md800_soc_snd_remove(struct platform_device *pdev)
{
	snd_soc_jack_notifier_unregister(&hp_jack, &hp_event);
	snd_soc_jack_free_gpios(&hp_jack, ARRAY_SIZE(hp_jack_gpios), hp_jack_gpios);
	snd_soc_unregister_card(&snd_soc_card_md800);
	gpio_free(md800_spk_gpio);

	return 0;
}

static struct platform_driver md800_soc_snd_driver = {
	.driver = {
		.name = "snd_soc_card_md800",
		.owner = THIS_MODULE,
	},
	.probe	= md800_soc_snd_probe,
	.remove = md800_soc_snd_remove,
};

module_platform_driver(md800_soc_snd_driver);

MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_DESCRIPTION("MD800 Board with ES8328 Codec for ALSA");
MODULE_LICENSE("GPL");

