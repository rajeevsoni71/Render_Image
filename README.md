# Render_Image
A small example to render a car image at boot time in android 12 before the bootanimation.



adding files for render image example and related sepolicy files
1. create a folders in below path
    mkdir -p device/generic/car/common/Render_image
    mkdir -p device/generic/car/common/Render_image/sepolicy
2. copy src code in Render_Image and rest sepolicy files copy in Render_image/sepolicy
3. Go to this path device/generic/car/common/car.mk and add below line to build as a part of your build system
      PRODUCT_PACKAGES += Render_Image
      # Additional selinux policy
     BOARD_SEPOLICY_DIRS += device/generic/car/common/Render_Image/sepolicy
