#
# Makefile for multiplexer I2C chip drivers.

obj-$(CONFIG_I2C_MUX_AMBARELLA)	+= i2c-mux-ambarella.o
obj-$(CONFIG_I2C_MUX_GPIO)	+= i2c-mux-gpio.o
obj-$(CONFIG_I2C_MUX_PCA9541)	+= i2c-mux-pca9541.o
obj-$(CONFIG_I2C_MUX_PCA954x)	+= i2c-mux-pca954x.o
obj-$(CONFIG_I2C_MUX_PINCTRL)	+= i2c-mux-pinctrl.o

ccflags-$(CONFIG_I2C_DEBUG_BUS) := -DDEBUG
