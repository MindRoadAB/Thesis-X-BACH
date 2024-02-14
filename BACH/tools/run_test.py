import math
import os
import pathlib
import subprocess
import sys
import time
from astropy.io import fits

TEST_TABLE = [
    # ID | Science       | Template        | HOTPANTS conv   | HOTPANTS sub   | Max abs error S,T | Max rel error S,T
    ( 1,   "test0",        "test1",          "test01_conv",    "test01_sub",    (0.0002, 0.0005),   (0.0005, 0.004 )),
    ( 2,   "testScience",  "testTemplate",   "testST_conv",    "testST_sub",    (0.008,  0.002 ),   (5e-6  , 0.9   )),
    ( 3,   "sparse0",      "sparse1",        "sparse01_conv",  "sparse01_sub",  (20,     5     ),   (0.0003, 0.0005))
]

ROOT_PATH = pathlib.Path(__file__).parent.parent.resolve()
BUILD_PATH = ROOT_PATH / "build" / "Debug"
RES_PATH = ROOT_PATH / "res"
TEST_PATH = ROOT_PATH / "tests"
OUTPUT_PATH = TEST_PATH / "out"

def diff_fits(h_path, b_path):
    h_file = fits.open(h_path)
    b_file = fits.open(b_path)
    
    assert(len(h_file) == 1)
    assert(len(b_file) == 1)

    h_data = h_file[0].data
    b_data = b_file[0].data
    
    assert(h_data.ndim == 2)
    assert(b_data.ndim == 2)

    max_error_abs = -10000000
    max_error_rel = -10000000
    
    for i in range(len(h_data)):
        for j in range(len(h_data[i])):
            h = h_data[i, j]
            b = b_data[i, j]

            if math.isnan(h) or math.isnan(b):
                if math.isnan(h) != math.isnan(b):
                    wrong_nans += 1
                
                continue

            error_abs = abs(h - b)
            
            if error_abs > max_error_abs:
                max_error_abs = error_abs

            if h > 0:
                error_rel = error_abs / h

                if error_rel > max_error_rel:
                    max_error_rel = error_rel

    h_file.close()
    b_file.close()

    return max_error_abs, max_error_rel

def main(args):
    exe_path = BUILD_PATH / "BACH.exe"

    print(f"There are a total of {len(TEST_TABLE)} tests to run")
    print(f"NOTE: running X-BACH from \"{BUILD_PATH.resolve()}\"")
    print()

    os.makedirs(OUTPUT_PATH, exist_ok=True)

    failed_tests = 0

    for test_case in TEST_TABLE:
        (id, science_name, template_name, conv_name, sub_name, max_abs_error, max_rel_error) = test_case

        print(f"Running test {id}...")

        args = [str(exe_path)]
        args += ["-ip", str(RES_PATH)]
        args += ["-s", f"{science_name}.fits"]
        args += ["-t", f"{template_name}.fits"]
        args += ["-op", str(OUTPUT_PATH / f"test{id}_")]

        start_time = time.time()

        with open(OUTPUT_PATH / f"test{id}_out.txt", "w") as out_stream:
            if not subprocess.run(args=args, stdout=out_stream, stderr=out_stream):
                failed_tests += 1
                print("X-BACH exited with an error code")
                print()
                continue

        end_time = time.time()
        test_time = end_time - start_time

        conv_abs_err, conv_rel_err = diff_fits(TEST_PATH / f"{conv_name}.fits", OUTPUT_PATH / f"test{id}_diff.fits")
        sub_abs_err, sub_rel_err = diff_fits(TEST_PATH / f"{sub_name}.fits", OUTPUT_PATH / f"test{id}_sub.fits")

        print(f"Test took {test_time:.2f} seconds")
        print(f"Convolution errors: {conv_abs_err} (abs) and {conv_rel_err} (rel)")
        print(f"Subtraction errors: {sub_abs_err} (abs) and {sub_rel_err} (rel)")

        test_fail = conv_abs_err > max_abs_error[0] or conv_rel_err > max_rel_error[0] or\
            sub_abs_err > max_abs_error[1] or sub_rel_err > max_rel_error[1]
        
        if test_fail:
            failed_tests += 1
            print(f"Test {id} failed on science image {science_name} and template image {template_name}")
        else:
            print(f"Test {id} succeeded")

        print()

    if failed_tests > 0:
        print(f"{failed_tests} tests failed.")

        sys.exit(1)
    else:
        print("All tests were successful.")

if __name__ == "__main__":
    main(sys.argv[1:])
