/ {
	images {
<<<<<<< HEAD
		fdt@xilfpga {
=======
		fdt-xilfpga {
>>>>>>> origin/android16-base
			description = "MIPSfpga (xilfpga) Device Tree";
			data = /incbin/("boot/dts/xilfpga/nexys4ddr.dtb");
			type = "flat_dt";
			arch = "mips";
			compression = "none";
<<<<<<< HEAD
			hash@0 {
=======
			hash {
>>>>>>> origin/android16-base
				algo = "sha1";
			};
		};
	};

	configurations {
<<<<<<< HEAD
		conf@xilfpga {
			description = "MIPSfpga Linux kernel";
			kernel = "kernel@0";
			fdt = "fdt@xilfpga";
=======
		conf-xilfpga {
			description = "MIPSfpga Linux kernel";
			kernel = "kernel";
			fdt = "fdt-xilfpga";
>>>>>>> origin/android16-base
		};
	};
};
