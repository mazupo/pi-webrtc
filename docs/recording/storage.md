# Storage

Give recordings a disk of their own, so they do not fill the system disk. Use a USB drive, or a disk image with a fixed size.

## Use a USB drive

1. Find the drive:

    ```bash
    sudo fdisk -l
    ```

2. Mount it at `/mnt/ext_disk` with `autofs` ([reference](https://wiki.gentoo.org/wiki/AutoFS)). Replace `/dev/sda1` with your drive:

    ```bash
    sudo apt install autofs
    echo '/- /etc/auto.usb --timeout=5' | sudo tee -a /etc/auto.master > /dev/null
    echo '/mnt/ext_disk -fstype=auto,nofail,nodev,nosuid,noatime,async,umask=000 :/dev/sda1' | sudo tee -a /etc/auto.usb > /dev/null
    sudo systemctl restart autofs
    ```

3. Record to it:

    ```bash
    ./pi-webrtc ... --record-path=/mnt/ext_disk/video
    ```

## Use a disk image

You do not need a USB drive. A disk image file limits recording to a fixed size.

1. Create a 16 GB image:

    ```bash
    dd if=/dev/zero of=/home/pi/16gb.img bs=1M count=16384
    ```

2. Format it:

    ```bash
    mkfs.ext4 /home/pi/16gb.img
    ```

3. Mount it:

    ```bash
    mkdir -p /home/pi/limited_folder
    sudo mount -o loop /home/pi/16gb.img /home/pi/limited_folder
    ```

4. Make it writable:

    ```bash
    sudo chmod 777 /home/pi/limited_folder
    ```

5. Record to it:

    ```bash
    ./pi-webrtc ... --record-path=/home/pi/limited_folder/
    ```

6. To mount it at every boot, add this line to `/etc/fstab`:

    ```
    /home/pi/16gb.img  /home/pi/limited_folder  ext4  loop,noatime  0  2
    ```

> [!CAUTION]
> A mistake in `/etc/fstab` can stop the system from booting. Test the mount by hand first.
