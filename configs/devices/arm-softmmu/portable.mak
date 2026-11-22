# Keep original default.mak unchanged. This standalone embedding build selects
# the existing iOS device models and ARM virt for redistribution-safe TCG tests.
CONFIG_IPOD_TOUCH_2G=y
CONFIG_IPOD_TOUCH_1G=y
CONFIG_IPAD1=y
CONFIG_SSI=y
CONFIG_PFLASH_CFI02=y
CONFIG_PL080=y
CONFIG_FRAMEBUFFER=y
# The A6 machine's USB host: the Exynos EHCI controller and its OHCI companions, which the
# N42 device tree describes and the guest's USB stack matches.
CONFIG_USB=y
CONFIG_USB_EHCI=y
CONFIG_USB_EHCI_SYSBUS=y
CONFIG_USB_OHCI=y
CONFIG_USB_OHCI_SYSBUS=y
