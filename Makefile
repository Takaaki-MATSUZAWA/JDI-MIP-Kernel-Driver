obj-m += jdi_mip.o

export KROOT=/lib/modules/$(shell uname -r)/build

all:  modules jdi_mip.dtbo

jdi_mip.dts:
	cpp -nostdinc \
		-I $(KROOT)/scripts/dtc/include-prefixes \
		-undef -x assembler-with-cpp \
		./jdi_mip.dtsi ./jdi_mip.dts

jdi_mip.dtbo: jdi_mip.dts
	dtc \
		-I dts -O dtb \
		-o ./jdi_mip.dtbo \
		./jdi_mip.dts


modules modules_install clean::
	@$(MAKE) -C $(KROOT) M=$(shell pwd) $@

clean::
	rm -rf   Module.symvers modules.order
	rm -f jdi_mip.dts jdi_mip.dtbo
