import cv2
import numpy as np
from skimage.metrics import structural_similarity as ssim

baselocation = "./skating_frames/frame_0048.png"
#testlocation = "./skating_frames/frame_0047.png"
#testlocation = "./oneslice_gop30_error_48.bmp"
#testlocation = "./mvc_48.png"
testlocation = "./hmve_48.png"

# 读取两张图像，图像必须大小相同才能计算
#img1 = cv2.imread(baselocation, cv2.IMREAD_GRAYSCALE)  # 将图像读取为灰度图
#img2 = cv2.imread(testlocation, cv2.IMREAD_GRAYSCALE)

# 计算 SSIM
#ssim_value, _ = ssim(img1, img2, full=True)

# 计算 PSNR
#psnr_value = cv2.PSNR(img1, img2)

#print(f"SSIM: {ssim_value}")
#print(f"PSNR: {psnr_value} dB")


def calculate_psnr(image1, image2):
    # 确保输入的两张图片是相同的尺寸
    if image1.shape != image2.shape:
        raise ValueError("输入的两张图片尺寸不同")

    # 计算均方误差 (MSE)
    mse = np.mean((image1 - image2) ** 2)

    # 如果MSE为0，说明两张图片完全相同，PSNR为无限大
    if mse == 0:
        return float('inf')

    # 计算PSNR
    pixel_max = 255.0  # 图像的最大像素值（假设图片是8位灰度图或RGB）
    psnr = 20 * np.log10(pixel_max / np.sqrt(mse))
    return psnr


# 加载图片
image1 = cv2.imread(baselocation)  # 第一张图片路径
image2 = cv2.imread(testlocation)  # 第二张图片路径

# 确保两张图片是RGB图像
if len(image1.shape) == 2:
    image1 = cv2.cvtColor(image1, cv2.COLOR_GRAY2BGR)
if len(image2.shape) == 2:
    image2 = cv2.cvtColor(image2, cv2.COLOR_GRAY2BGR)

# 分离图像的三个通道
image1_b, image1_g, image1_r = cv2.split(image1)
image2_b, image2_g, image2_r = cv2.split(image2)

# 计算每个通道的PSNR
psnr_r = calculate_psnr(image1_r, image2_r)
psnr_g = calculate_psnr(image1_g, image2_g)
psnr_b = calculate_psnr(image1_b, image2_b)

print(f"Blue通道的PSNR值: {(psnr_r+psnr_g+psnr_b)/3} dB")