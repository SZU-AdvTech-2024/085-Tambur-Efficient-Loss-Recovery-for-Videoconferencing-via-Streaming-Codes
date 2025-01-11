from PIL import Image
width, height = 704,576

# 定义一个字典来存储按 framenum 分类的行数据
data_by_framenum = {}

# 打开并读取文件
with open('skating_mv.txt', 'r') as file:
    for line in file:
        # 跳过空行或注释行（如果有的话）
        if not line.strip():
            continue

        # 解析每一行数据
        parts = line.strip().split(',')

        # 提取 framenum 和其它数据
        framenum = int(parts[0])

        # 将其余的数据保存在一个元组中
        data = tuple(map(int, parts[1:]))

        # 根据 framenum 分类，将数据存入字典
        if framenum not in data_by_framenum:
            data_by_framenum[framenum] = []

        data_by_framenum[framenum].append(data)

# 打印按 framenum 分类后的数据
for framenum, data_list in data_by_framenum.items():
    if framenum == 48:
        print(f"Framenum {framenum}:")
        #image = Image.new("RGB", (width, height), (255, 255, 255))  # 创建白色背景图像
        image = Image.open('./skating_frames/frame_'+str(framenum-1).zfill(4)+".png")
        # 获取像素数据
        pixels = image.load()
        im0 = Image.open('./skating_frames/frame_'+str(framenum-1).zfill(4)+".png")
        #im1 = Image.open('./frames/frame_000'+str(framenum+1)+".png")
        for data in data_list:
            source = data[0]
            block_width = data[1]
            block_height = data[2]
            src_x = data[3]
            src_y = data[4]
            dst_x = data[5]
            dst_y = data[6]

            for i in range(block_width):
                for j in range(block_height):
                    if(dst_x+i>=width or dst_y+j>=height or src_x+i>=width or src_y+j>=height): continue

                    if(source == -1):
                        pixels[dst_x+i, dst_y+j] = im0.getpixel((src_x+i, src_y+j))
                    #else:
                        #pixels[dst_x+i, dst_y+j] = im1.getpixel((src_x+i, src_y+j))

        image.save("mvc_"+str(framenum)+".png")


