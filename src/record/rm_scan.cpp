/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_scan.h"
#include "rm_file_handle.h"

/**
 * @brief 初始化file_handle和rid
 * @param file_handle
 */
RmScan::RmScan(const RmFileHandle *file_handle) : file_handle_(file_handle) {
    // Todo:
    // 初始化file_handle和rid（指向第一个存放了记录的位置）
    // 初始化file_handle_
    this->file_handle_ = file_handle;  // 保存文件句柄指针，用于后续页面访问

    // 记录id初始化为首记录
    this->rid_.page_no =  RM_FIRST_RECORD_PAGE;  // 从第一个记录页面开始扫描
    this->rid_.slot_no = -1;  // slot_no初始化为-1，表示还未找到有效slot

    // 找到第一条记录的页号和槽号
    next();  // 调用next()方法定位到第一条记录
}

/**
 * @brief 找到文件中下一个存放了记录的位置
 */
void RmScan::next() {
    // Todo:
    // 找到文件中下一个存放了记录的非空闲位置，用rid_来指向这个位置

    // 循环遍历所有页面，直到找到一个有效的记录位置
    while(this->rid_.page_no < file_handle_ -> file_hdr_.num_pages){
        // 步骤1: 获取当前页面的句柄
        RmPageHandle page_handle = file_handle_->fetch_page_handle(this->rid_.page_no);

        // 步骤2: 在当前页面的bitmap中查找下一个设置为1的位（表示有记录的slot）
        // 从rid_.slot_no + 1开始查找（因为slot_no初始为-1，所以从0开始）
        this->rid_.slot_no = Bitmap::next_bit(true, page_handle.bitmap,
                                              file_handle_->file_hdr_.num_records_per_page,
                                              this->rid_.slot_no);

        // 步骤3: 检查是否在本页找到了有效slot
        if(this->rid_.slot_no >= this->file_handle_->file_hdr_.num_records_per_page){  //本页没有
            // 当前页面没有找到记录，准备进入下一页
            if ((this->rid_.page_no + 1) == file_handle_ -> file_hdr_.num_pages){ // 遍历后续所有页，未找到
                // 已经到达最后一页，没有找到任何记录
                this->rid_ = Rid{RM_NO_PAGE, -1};  // 设置为无效rid表示扫描结束
                break;
            }
            else{
                // 还有下一页，继续扫描
                this->rid_ = Rid{this->rid_.page_no+1, -1};  //获取下一页，下一个循环继续找下一页
            }
        }
        else{
            // 在当前页面找到了有效slot，停止扫描
            break;
        }
    }
}

/**
 * @brief ​ 判断是否到达文件末尾
 */
bool RmScan::is_end() const {
    // Todo: 修改返回值

    // 检查当前rid是否指向无效位置（RM_NO_PAGE表示扫描结束）
    return rid_.page_no == RM_NO_PAGE;
}

/**
 * @brief RmScan内部存放的rid
 */
Rid RmScan::rid() const {
    // 返回当前扫描器指向的记录ID
    return rid_;
}