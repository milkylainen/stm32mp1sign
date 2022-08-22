# stm32mp1sign

**_THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT._**

stm32mp1sign is a C replacement for STM32MP_SigningTool_CLI (closed source) in STM32CubeProgrammer.

Functionally implemented from the descriptions at:

[STM32MP1 Secure Boot Description](https://wiki.st.com/stm32mpu/wiki/STM32_MPU_ROM_code_secure_boot)

[STM32MP1 KeyGen Tool Description](https://wiki.st.com/stm32mpu/wiki/KeyGen_tool)

[STM32MP1 STM32Key U-Boot Command](https://wiki.st.com/stm32mpu/wiki/How_to_use_U-Boot_stm32key_command)

[STM32MP1 Sign Tool Description](https://wiki.st.com/stm32mpu/wiki/Signing_tool)

**_Beware. Fusing a device	means you can never undo the key.
If you loose the private key or	password, then that's it.
No more	signing	new images.
If you also close the device then there	will be	no other way out.
You have been warned!_**

1. Generate keys. Here you can use STM32CubeProgrammer or regular OpenSSL.
This should get	you a prime256v1 (default) curve key with	the privkey encrypted in AES256 (default).
**_DO NOT LOOSE THE KEYS!_**

```
$. STM32MP_KeyGen_CLI -abs /home/user/KeyFolder/ -pwd qwerty
```

2. Copy	the hash of the	public key to U-boot and fuse it there. (WARNING!)
```
> tftpboot 0xc0000000 publicKeysHash.bin
> stm32key read 0xc0000000
> stm32key fuse 0xc0000000
```
3. You can now use stm32mp1sign	or STM32MP_SigningTool_CLI to sign your	TF-A (fsbl) binary.
Using stm32mp1sign, the	image is modified in situ. The key path must contain a private key
with a public key included. If a password is not specified, stm32mp1sign will ask for one.
```
$ stm32mp1sign --image path/to/tf-a-binary --key path/to/privkey --password qwerty
```
