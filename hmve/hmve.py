from contextlib import nullcontext

from PIL import Image

class MotionVector:
    def __init__(self, x, y):
        """初始化运动矢量"""
        self.x = x
        self.y = y

    def __repr__(self):
        return f"({self.x}, {self.y})"

    def get_motion_x(self):
        return self.x

    def get_motion_y(self):
        return self.y


class Pixel:
    def __init__(self):
        """初始化一个像素点，包含多个运动矢量"""
        self.motion_vectors = []

    def add_motion_vector(self, mv):
        """向像素点添加一个运动矢量"""
        if isinstance(mv, MotionVector):
            self.motion_vectors.append(mv)
        else:
            raise ValueError("只能添加 MotionVector 类型的对象")

    def get_motion_vectors(self):
        return self.motion_vectors

    def __repr__(self):
        return f"Pixel(Motion Vectors: {self.motion_vectors})"


class Block:
    def __init__(self):
        """初始化一个像素点，包含多个运动矢量"""
        self.motion_vectors = []
        self.tmp = 0
        self.cur_mv = None
        self.best = 0
        self.best_mv = None
        self.avg_mv = None

    def add_motion_vector(self, mv):
        """向像素点添加一个运动矢量"""
        if isinstance(mv, MotionVector):
            if mv in self.motion_vectors:
                self.tmp += 1
            else:
                if self.tmp > self.best:
                    self.best_mv = self.cur_mv
                    self.best = self.tmp
                self.tmp = 1
                self.cur_mv = mv
                self.motion_vectors.append(mv)
        else:
            raise ValueError("只能添加 MotionVector 类型的对象")

    def get_best_mv(self):
        return self.best_mv

    def get_avg_mv(self):
        return self.avg_mv

    def cal_avg_mv(self):
        if self.tmp > self.best:
            self.best_mv = self.cur_mv
            self.best = self.tmp

        if self.motion_vectors:
            sx = 0
            sy = 0
            for mv in self.motion_vectors:
                sx += mv.x
                sy += mv.y
            self.avg_mv = MotionVector(sx / len(self.motion_vectors), sy / len(self.motion_vectors))

    def __repr__(self):
        return f"Pixel(Motion Vectors: {self.motion_vectors})"

class Frame:
    def __init__(self, n, m):
        """初始化一个 n*m 大小的图片"""
        self.n = n
        self.m = m
        self.pixels = [[Pixel() for _ in range(m)] for _ in range(n)]
        self.blocks = [[Block() for _ in range(int(m/8))] for _ in range(int(n/8))]

    def add_motion_vector_to_pixel(self, row, col, mv):
        """向指定像素添加一个运动矢量"""
        if 0 <= row < self.n and 0 <= col < self.m:
            self.pixels[row][col].add_motion_vector(mv)
        else:
            raise IndexError("指定的像素位置超出图像范围")

    def get_pixel(self, row, col):
        """获取指定位置的像素点"""
        if 0 <= row < self.n and 0 <= col < self.m:
            return self.pixels[row][col]
        else:
            raise IndexError("指定的像素位置超出图像范围")

    def add_motion_vector_to_block(self, row, col, mv):
        """向指定宏块添加一个运动矢量"""
        if 0 <= row < self.n and 0 <= col < self.m:
            self.get_block_for_pixel(row, col).add_motion_vector(mv)
        else:
            raise IndexError("指定的像素位置超出图像范围")

    def update_blocks(self):
        for block in self.blocks:
            for b in block:
                b.cal_avg_mv()

    def get_block_for_pixel(self, row, col):
        return self.blocks[int(row/8)][int(col/8)]

    def __repr__(self):
        return f"Image({self.n}x{self.m} pixels)"


width, height = 704,576

def mv_extrapolation(image:Frame, data_list:list):
    for data in data_list:
        source = data[0]
        block_width = data[1]
        block_height = data[2]
        src_x = data[3]
        src_y = data[4]
        dst_x = data[5]
        dst_y = data[6]
        mv_x = data[7]
        mv_y = data[8]
        if mv_x != 0 and mv_y != 0:

            if mv_x != 0 or mv_y != 0:
                mve_x = dst_x + (dst_x - src_x)
                mve_y = dst_y + (dst_y - src_y)
                for i in range(block_width):
                    for j in range(block_height):
                        if (mve_x + i >= width or mve_y + j >= height or mve_x + i < 0 or mve_y + j < 0): continue
                        #pixel level
                        image.add_motion_vector_to_pixel(mve_x + i, mve_y + j, MotionVector(mv_x, mv_y))
                        #block level
                        image.add_motion_vector_to_block(mve_x + i, mve_y + j, MotionVector(mv_x, mv_y))

def error_concealment(frame:Frame,framenum:int):
    curimage =  Image.new("RGB", (width, height), (255, 255, 255))
    pixels = curimage.load()
    preimage = Image.open('./skating_frames/frame_' + str(framenum - 1).zfill(4) + ".png")

    frame.update_blocks()
    for i in range(width):
        for j in range(height):
            motion_vectors = frame.get_pixel(i, j).get_motion_vectors()
            motion_vector_m = frame.get_block_for_pixel(i, j).get_best_mv()
            motion_vector_a = frame.get_block_for_pixel(i,j).get_avg_mv()
            fx , fy = 0, 0
            if len(motion_vectors)>0:
                sx, sy = 0,0
                for motion_vector in motion_vectors:
                    sx += motion_vector.x
                    sy += motion_vector.y
                ax = sx/ len(motion_vectors)
                ay = sy/ len(motion_vectors)

                fx = (ax + motion_vector_a.x + motion_vector_m.x) / 3
                fy = (ay + motion_vector_a.y + motion_vector_m.y) / 3

            elif motion_vector_m is not None:
                fx = (motion_vector_a.x + motion_vector_m.x) / 2
                fy = (motion_vector_a.y + motion_vector_m.y) / 2



            px = i + (fx / 4)
            py = j + (fy / 4)
            if px >= width: px = width - 1
            if py >= height: py = height - 1
            if px < 0: px = 0
            if py < 0: py = 0


            pixels[i, j] = preimage.getpixel((px, py))

    curimage.save("hmve_" + str(framenum) + ".png")

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


tmp = None
for framenum, data_list in data_by_framenum.items():
    if framenum == 47:
        tmp = data_list
    if framenum == 48:
        curframe = Frame(width, height)
        mv_extrapolation(curframe, tmp)
        error_concealment(curframe,framenum)


