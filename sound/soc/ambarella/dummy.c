/*
 * sound/soc/dummy.c
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 * History:
 *	2009/03/12 - [Cao Rongrong] Created file
 *	2009/06/10 - [Cao Rongrong] Port to 2.6.29
 *	2011/03/20 - [Cao Rongrong] Port to 2.6.38
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
#include <linux/gpio.h>
#include <linux/init.h>

#include <sound/soc.h>

#include <plat/audio.h>

#include "../codecs/ambarella_dummy.h"

static unsigned int dummy_dai_fmt = 0;
module_param(dummy_dai_fmt, uint, 0644);
MODULE_PARM_DESC(dummy_dai_fmt, "DAI format.");

static unsigned int dummy_disable_codec = 0;
module_param(dummy_disable_codec, uint, 0644);
MODULE_PARM_DESC(dummy_disable_codec, "Disable External Codec.");

static unsigned int dummy_pwr_pin = 12;
module_param(dummy_pwr_pin, uint, 0644);
MODULE_PARM_DESC(dummy_pwr_pin, "External Codec Power Pin.");


static int ambarella_dummy_board_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int errorCode = 0, mclk, oversample, i2s_mode;

	switch (params_rate(params)) {
	case 8000:
		mclk = 4096000;
		oversample = AudioCodec_512xfs;
		break;
	case 11025:
		mclk = 5644800;
		oversample = AudioCodec_512xfs;
		break;
	case 16000:
		mclk = 4096000;
		oversample = AudioCodec_256xfs;
		break;
	case 22050:
		mclk = 5644800;
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

	if (dummy_dai_fmt == 0)
		i2s_mode = SND_SOC_DAIFMT_I2S;
	else
		i2s_mode = SND_SOC_DAIFMT_DSP_A;

	/* set the I2S system data format*/
	errorCode = snd_soc_dai_set_fmt(cpu_dai,
		i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
	if (errorCode < 0) {
		printk(KERN_ERR "can't set cpu DAI configuration\n");
		goto hw_params_exit;
	}

	/* set the I2S system clock*/
	errorCode = snd_soc_dai_set_sysclk(cpu_dai, AMBARELLA_CLKSRC_ONCHIP, mclk, 0);
	if (errorCode < 0) {
		printk(KERN_ERR "can't set cpu MCLK configuration\n");
		goto hw_params_exit;
	}

	errorCode = snd_soc_dai_set_clkdiv(cpu_dai, AMBARELLA_CLKDIV_LRCLK, oversample);
	if (errorCode < 0) {
		printk(KERN_ERR "can't set cpu MCLK/SF ratio\n");
		goto hw_params_exit;
	}

hw_params_exit:
	return errorCode;
}


static struct snd_soc_ops ambarella_dummy_board_ops = {
	.hw_params = ambarella_dummy_board_hw_params,
};

static struct snd_soc_dai_link ambarella_dummy_dai_link = {
	.name = "AMB-DUMMY",
	.stream_name = "AMB-DUMMY-STREAM",
	.cpu_dai_name = "ambarella-i2s.0",
	.platform_name = "ambarella-pcm-audio",
	.codec_dai_name = "AMBARELLA_DUMMY_CODEC",
	.codec_name = "ambdummy-codec",
	.ops = &ambarella_dummy_board_ops,
};

static struct snd_soc_card snd_soc_card_ambarella_dummy = {
	.name = "ambarella_dummy",
	.owner = THIS_MODULE,
	.dai_link = &ambarella_dummy_dai_link,
	.num_links = 1,
};

static int ambdummy_soc_snd_probe(struct platform_device *pdev)
{
	int errorCode = 0;

	snd_soc_card_ambarella_dummy.dev = &pdev->dev;
	errorCode = snd_soc_register_card(&snd_soc_card_ambarella_dummy);
	if (errorCode)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", errorCode);

	if (dummy_disable_codec) {
		errorCode = gpio_request_one(dummy_pwr_pin, GPIOF_OUT_INIT_LOW,
				"dummy_disable_codec");
		if (errorCode < 0) {
			pr_err("Request dummy_disable_codec GPIO(%d) failed\n",
				dummy_pwr_pin);
		}
	}

	return errorCode;
}

static int ambdummy_soc_snd_remove(struct platform_device *pdev)
{
	if (dummy_disable_codec)
		gpio_free(dummy_pwr_pin);

	snd_soc_unregister_card(&snd_soc_card_ambarella_dummy);

	return 0;
}

static struct platform_driver ambdummy_soc_snd_driver = {
	.driver = {
		.name = "snd_soc_card_ambdummy",
		.owner = THIS_MODULE,
	},
	.probe	= ambdummy_soc_snd_probe,
	.remove = ambdummy_soc_snd_remove,
};

module_platform_driver(ambdummy_soc_snd_driver);

MODULE_AUTHOR("Anthony Ginger <hfjiang@ambarella.com>");
MODULE_DESCRIPTION("Amabrella A2 Board with internal Codec for ALSA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("snd-soc-a2bub");

