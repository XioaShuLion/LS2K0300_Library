# SPDX-License-Identifier: GPL-2.0



drivers/seekree_libraries/zf_device/zf_device_config.o:
	cp drivers/seekree_libraries/zf_device/zf_device_config.elf drivers/seekree_libraries/zf_device/zf_device_config.o

obj-$(CONFIG_ZF_DEVICE_IMU) += zf_device_imu.o
zf_device_imu-objs := 	zf_device/zf_device_imu.o \
						zf_device/zf_device_imu_core.o \
						zf_device/zf_device_imu660ra.o \
						zf_device/zf_device_imu660rb.o \
						zf_device/zf_device_imu660rc.o \
						zf_device/zf_device_imu963ra.o \
						zf_device/zf_device_config.o 

obj-$(CONFIG_ZF_DEVICE_DL1X) += zf_device_dl1x.o
zf_device_dl1x-objs := zf_device/zf_device_dl1x.o zf_device/zf_device_config.o zf_device/zf_device_dl1x_core.o zf_device/zf_device_dl1a.o zf_device/zf_device_dl1b.o

# obj-$(CONFIG_ZF_DRIVER_ATIM) += zf_driver_atim.o
# zf_driver_atim-objs := zf_driver/zf_driver_atim.o

# obj-$(CONFIG_ZF_DRIVER_SPI_IO) += zf_driver_spi_io.o
# zf_driver_spi_io-objs := zf_driver/zf_driver_spi_io.o

# obj-$(CONFIG_ZF_DRIVER_ENCODER) += zf_driver_encoder.o
# zf_driver_encoder-objs := zf_driver/zf_driver_encoder.o

obj-$(CONFIG_ZF_DRIVER_GPIO) += zf_driver_gpio.o
zf_driver_gpio-objs := zf_driver/zf_driver_gpio.o 

# obj-$(CONFIG_ZF_DRIVER_GTIM) += zf_driver_gtim.o
# zf_driver_gtim-objs := zf_driver/zf_driver_gtim.o

obj-$(CONFIG_ZF_DRIVER_PWM) += zf_driver_pwm.o
zf_driver_pwm-objs := zf_driver/zf_driver_pwm.o 

obj-$(CONFIG_ZF_DRIVER_ENCODER) += zf_driver_encoder.o
zf_driver_encoder-objs := \
	zf_driver/zf_driver_atim.o \
	zf_driver/zf_driver_gtim.o \
	zf_driver/zf_driver_encoder_atim.o \
	zf_driver/zf_driver_encoder_gtim.o \
	zf_driver/zf_driver_encoder_core.o \
	zf_driver/zf_driver_encoder.o 

