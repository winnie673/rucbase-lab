/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "rm_file_handle.h"

/**
 * @description: 获取当前表中记录号为rid的记录
 * @param {Rid&} rid 记录号，指定记录的位置
 * @param {Context*} context
 * @return {unique_ptr<RmRecord>} rid对应的记录对象指针
 */
std::unique_ptr<RmRecord> RmFileHandle::get_record(const Rid& rid, Context* context) const {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 初始化一个指向RmRecord的指针（赋值其内部的data和size）

    // 步骤1: 获取指定记录所在的页面句柄
    auto page_handle = fetch_page_handle(rid.page_no); // 取指定记录所在的page handle

    // 步骤2: 创建一个新的RmRecord对象来存储记录数据
    auto record = std::make_unique<RmRecord>(file_hdr_.record_size);

    // 步骤3: 检查指定slot是否确实包含记录
    if(!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {  // 是否找到record
        throw RecordNotFoundError(rid.page_no, rid.slot_no);  // 如果没有记录，抛出异常
    }

    // 步骤4: 从页面中复制记录数据到record对象
    memcpy(record->data, page_handle.get_slot(rid.slot_no), file_hdr_.record_size);  // .get_slot()返回位于slot_no的record的地址
    record->size = file_hdr_.record_size;  // 设置记录大小

    // 步骤5: 返回记录对象的智能指针
    return record;
}

/**
 * @description: 在当前表中插入一条记录，不指定插入位置
 * @param {char*} buf 要插入的记录的数据
 * @param {Context*} context
 * @return {Rid} 插入的记录的记录号（位置）
 */
Rid RmFileHandle::insert_record(char* buf, Context* context) {
    // Todo:
    // 1. 获取当前未满的page handle
    // 2. 在page handle中找到空闲slot位置
    // 3. 将buf复制到空闲slot位置
    // 4. 更新page_handle.page_hdr中的数据结构
    // 注意考虑插入一条记录后页面已满的情况，需要更新file_hdr_.first_free_page_no

    // 步骤1: 获取一个可用的页面句柄（空闲页面或新页面）
    RmPageHandle page_handle = create_page_handle(); // 创建或获取一个空闲的page handle

    // 步骤2: 在页面的bitmap中找到第一个空闲的slot位置
    int free_slot = Bitmap::first_bit(false, page_handle.bitmap, file_hdr_.num_records_per_page);  // 获取空闲的slot
    //false表示找到第一个未设置的比特位，传入page_handle的位图和该页最大记录数。Bitmap::first_bit会扫描位图，找到第一个为0的slot位。即查找页面中第一个空闲的记录槽位，并返回该空闲slot的序号。

    // 步骤3: 将记录数据复制到找到的空闲slot位置
    memcpy(page_handle.get_slot(free_slot), buf, file_hdr_.record_size);

    // 步骤4: 更新bitmap，标记该slot已被占用
    Bitmap::set(page_handle.bitmap, free_slot);

    // 步骤5: 更新页面头信息
    // 更新page_handle.page_hdr
    page_handle.page_hdr->num_records+=1;  // 记录数加1

    // 步骤6: 检查页面是否已满，如果已满则更新文件头的空闲页面链表
    if(page_handle.page_hdr->num_records == file_hdr_.num_records_per_page) {  // 满了
        file_hdr_.first_free_page_no = page_handle.page_hdr->next_free_page_no;  // file_hdr_.first_free_page_no从被占满的此页变为此页指向的next_free_page_no
    }

    // 步骤7: 返回新插入记录的Rid
    return Rid{page_handle.page->get_page_id().page_no, free_slot};
}

/**
 * @description: 在当前表中的指定位置插入一条记录
 * @param {Rid&} rid 要插入记录的位置
 * @param {char*} buf 要插入记录的数据
 */
void RmFileHandle::insert_record(const Rid& rid, char* buf) {
    // 步骤1: 检查目标页面是否存在，如果不存在则创建新页面
    if (rid.page_no < file_hdr_.num_pages) {
        create_new_page_handle();  // 这里可能有逻辑错误，应该是检查页面是否不存在时创建
    }

    // 步骤2: 获取目标页面的句柄
    RmPageHandle pageHandle = fetch_page_handle(rid.page_no);

    // 步骤3: 在bitmap中设置指定slot为已占用
    Bitmap::set(pageHandle.bitmap, rid.slot_no);

    // 步骤4: 更新页面头中的记录数
    pageHandle.page_hdr->num_records++;

    // 步骤5: 检查页面是否已满，如果已满则更新空闲页面链表
    if (pageHandle.page_hdr->num_records == file_hdr_.num_records_per_page) {
        file_hdr_.first_free_page_no = pageHandle.page_hdr->next_free_page_no;
    }

    // 步骤6: 将记录数据复制到指定slot位置
    char *slot = pageHandle.get_slot(rid.slot_no);
    memcpy(slot, buf, file_hdr_.record_size);

    // 步骤7: 取消页面的固定状态（因为页面已被修改）
    buffer_pool_manager_->unpin_page(pageHandle.page->get_page_id(), true);
}

/**
 * @description: 删除记录文件中记录号为rid的记录
 * @param {Rid&} rid 要删除的记录的记录号（位置）
 * @param {Context*} context
 */
void RmFileHandle::delete_record(const Rid& rid, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新page_handle.page_hdr中的数据结构
    // 注意考虑删除一条记录后页面未满的情况，需要调用release_page_handle()

    // 步骤1: 获取指定记录所在的页面句柄
    auto page_handle = fetch_page_handle(rid.page_no); // 取指定记录所在的page handle

    // 步骤2: 检查指定slot是否确实包含记录
    if(!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) { // 是否找到record
        throw PageNotExistError("  ", rid.page_no);  // 如果没有记录，抛出异常
    }

    // 步骤3: 在bitmap中重置指定slot为未占用
    Bitmap::reset(page_handle.bitmap, rid.slot_no);  // 置0

    // 步骤4: 更新页面头中的记录数
    page_handle.page_hdr->num_records -= 1;

    // 步骤5: 检查删除记录后页面是否从满变为空闲，如果是则将其加入空闲页面链表
    // 这里判断删除后记录数是否等于满页记录数减1，表示页面从满变为未满
    if(page_handle.page_hdr->num_records == file_hdr_.num_records_per_page - 1) {
        release_page_handle(page_handle);
    }
}


/**
 * @description: 更新记录文件中记录号为rid的记录
 * @param {Rid&} rid 要更新的记录的记录号（位置）
 * @param {char*} buf 新记录的数据
 * @param {Context*} context
 */
void RmFileHandle::update_record(const Rid& rid, char* buf, Context* context) {
    // Todo:
    // 1. 获取指定记录所在的page handle
    // 2. 更新记录

    // 步骤1: 获取指定记录所在的页面句柄
    auto page_handle = fetch_page_handle(rid.page_no); // 取指定记录所在的page handle

    // 步骤2: 检查指定slot是否确实包含记录
    if(!Bitmap::is_set(page_handle.bitmap, rid.slot_no)) {  // 是否找到record
        throw PageNotExistError("  ", rid.page_no);  // 如果没有记录，抛出异常
    }

    // 步骤3: 将新的记录数据复制到指定slot位置，覆盖原有数据
    memcpy(page_handle.get_slot(rid.slot_no), buf, file_hdr_.record_size);  // 更新record
}

/**
 * 以下函数为辅助函数，仅提供参考，可以选择完成如下函数，也可以删除如下函数，在单元测试中不涉及如下函数接口的直接调用
*/
/**
 * @description: 获取指定页面的页面句柄
 * @param {int} page_no 页面号
 * @return {RmPageHandle} 指定页面的句柄
 */
RmPageHandle RmFileHandle::fetch_page_handle(int page_no) const {
    // Todo:
    // 使用缓冲池获取指定页面，并生成page_handle返回给上层
    // if page_no is invalid, throw PageNotExistError exception

    // 步骤1: 检查页面号是否合法
    if(page_no >= file_hdr_.num_pages) {
        throw PageNotExistError(" ", page_no);  // 如果页面号无效，抛出异常
    }

    // 步骤2: 从buffer pool中获取指定页面，并生成page_handle返回
    return RmPageHandle(&file_hdr_, buffer_pool_manager_->fetch_page({fd_, page_no}));
}

/**
 * @description: 创建一个新的page handle
 * @return {RmPageHandle} 新的PageHandle
 */
RmPageHandle RmFileHandle::create_new_page_handle() {
    // Todo:
    // 1.使用缓冲池来创建一个新page
    // 2.更新page handle中的相关信息
    // 3.更新file_hdr_

    // 步骤1: 创建新页面的ID（页面号暂时设为INVALID_PAGE_ID，由buffer pool确定）
    PageId page_id = {this->fd_, INVALID_PAGE_ID};  // 新页id未确定，由NewPage()确定

    // 步骤2: 使用缓冲池创建一个新页面
    Page* newPage = this->buffer_pool_manager_->new_page(&page_id);

    // 步骤3: 创建页面句柄
    RmPageHandle page_handle = RmPageHandle(&file_hdr_, newPage);

    // 步骤4: 如果页面创建成功，初始化页面头部信息并更新文件头
    if(newPage != nullptr) {
        page_handle.page_hdr->num_records = 0;  // 初始化记录数为0
        page_handle.page_hdr->next_free_page_no = RM_NO_PAGE;  // 下一空闲页设为NO_PAGE
        file_hdr_.num_pages+=1;  // 更新文件头中的页面总数
        file_hdr_.first_free_page_no = page_id.page_no;  // 更新第一空闲页号
    }

    // 步骤5: 返回新创建的页面句柄
    return page_handle;
}

/**
 * @brief 创建或获取一个空闲的page handle
 *
 * @return RmPageHandle 返回生成的空闲page handle
 * @note pin the page, remember to unpin it outside!
 */
RmPageHandle RmFileHandle::create_page_handle() {
    // Todo:
    // 1. 判断file_hdr_中是否还有空闲页
    //     1.1 没有空闲页：使用缓冲池来创建一个新page；可直接调用create_new_page_handle()
    //     1.2 有空闲页：直接获取第一个空闲页
    // 2. 生成page handle并返回给上层

    // 步骤1: 检查是否有空闲页面可用
    if(file_hdr_.first_free_page_no != RM_NO_PAGE){
        // 步骤1.1: 如果有空闲页，直接获取第一个空闲页
        return fetch_page_handle(file_hdr_.first_free_page_no);
    } else {
        // 步骤1.2: 如果没有空闲页，创建一个新页面
        return create_new_page_handle();
    }
}

/**
 * @description: 当一个页面从没有空闲空间的状态变为有空闲空间状态时，更新文件头和页头中空闲页面相关的元数据
 */
void RmFileHandle::release_page_handle(RmPageHandle&page_handle) {
    // Todo:
    // 当page从已满变成未满，考虑如何更新：
    // 1. page_handle.page_hdr->next_free_page_no
    // 2. file_hdr_.first_free_page_no

    // 步骤1: 将该页面加入到空闲页面链表的头部
    page_handle.page_hdr->next_free_page_no = file_hdr_.first_free_page_no;  // 设置该页面的下一空闲页为当前第一空闲页

    // 步骤2: 更新文件头中的第一空闲页号为当前页面
    file_hdr_.first_free_page_no = page_handle.page->get_page_id().page_no;  // 将当前页面设为第一空闲页

    /* 更新方式
       file->first_free(old)
       page->next_free=first_free(old)
       file->first_free(new) = page_no
       file-->first_free(new)（this page)--> first_free(old)*/
}