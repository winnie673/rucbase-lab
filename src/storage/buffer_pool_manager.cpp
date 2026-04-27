/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "buffer_pool_manager.h"

/**
 * @description: 从free_list或replacer中得到可淘汰帧页的 *frame_id
 * @return {bool} true: 可替换帧查找成功 , false: 可替换帧查找失败
 * @param {frame_id_t*} frame_id 帧页id指针,返回成功找到的可替换帧id
 */
bool BufferPoolManager::find_victim_page(frame_id_t* frame_id) {
    // Todo:
    // 1 使用BufferPoolManager::free_list_判断缓冲池是否已满需要淘汰页面
    // 1.1 未满获得frame
    // 1.2 已满使用lru_replacer中的方法选择淘汰页面

    // 检查空闲列表是否为空，如果不为空则直接从空闲列表获取帧
    if (this->free_list_.empty()) {
        // 空闲列表为空，说明缓冲池已满，需要使用LRU替换器选择一个牺牲页面
        if (!this->replacer_->victim(frame_id)) { // 空闲帧不足,调用LRU淘汰
            return false; // 淘汰失败
        }
    }
    else {
        // 还有空闲帧，直接从空闲列表前端取出一个帧
        *frame_id = this->free_list_.front();// 还有空闲帧,直接使用
        this->free_list_.pop_front();
    }
    return true;
}

/**
 * @description: 更新页面数据, 如果为脏页则需写入磁盘，再更新为新页面，更新page元数据(data, is_dirty, page_id)和page table
 * @param {Page*} page 写回页指针
 * @param {PageId} new_page_id 新的page_id
 * @param {frame_id_t} new_frame_id 新的帧frame_id
 */
void BufferPoolManager::update_page(Page *page, PageId new_page_id, frame_id_t new_frame_id) {
    // Todo:
    // 1 如果是脏页，写回磁盘，并且把dirty置为false
    // 2 更新page table
    // 3 重置page的data，更新page id

    // 步骤1: 处理脏页写回磁盘
    // 如果当前页面是脏页（已被修改），需要先将其写回磁盘以确保数据持久性
    if(page->is_dirty()) {  //脏位处理
        this->disk_manager_->write_page(page->get_page_id().fd, page->get_page_id().page_no, page->get_data(), PAGE_SIZE);
        page->is_dirty_ = false;  // 写回后清除脏位标记
    }

    // 步骤2: 重置页面内存数据，为新页面准备
    page->reset_memory();  // 将页面数据重置为空，准备存放新内容

    // 步骤3: 更新页表映射关系
    // 从页表中移除旧的页面ID映射（如果存在）
    for(auto position = this->page_table_.begin(); position != this->page_table_.end(); position++) {  //更新table
        if(position->first == page->id_) {
            this->page_table_.erase(position);
            break;
        }
    }
    // 添加新的页面ID到帧ID的映射
    this->page_table_[new_page_id] = new_frame_id;

    // 步骤4: 更新页面的元数据
    page->id_ = new_page_id;  // 设置新的页面ID
}

/**
 * @description: 从buffer pool获取需要的页。
 *              如果页表中存在page_id（说明该page在缓冲池中），并且pin_count++。
 *              如果页表不存在page_id（说明该page在磁盘中），则找缓冲池victim page，将其替换为磁盘中读取的page，pin_count置1。
 * @return {Page*} 若获得了需要的页则将其返回，否则返回nullptr
 * @param {PageId} page_id 需要获取的页的PageId
 */
Page* BufferPoolManager::fetch_page(PageId page_id) {
    //Todo:
    // 1.     从page_table_中搜寻目标页
    // 1.1    若目标页有被page_table_记录，则将其所在frame固定(pin)，并返回目标页。
    // 1.2    否则，尝试调用find_victim_page获得一个可用的frame，若失败则返回nullptr
    // 2.     若获得的可用frame存储的为dirty page，则须调用updata_page将page写回到磁盘
    // 3.     调用disk_manager_的read_page读取目标页到frame
    // 4.     固定目标页，更新pin_count_
    // 5.     返回目标页

    // 使用互斥锁保护并发访问，确保线程安全
    std::scoped_lock lock{latch_};

    frame_id_t id;  // 用于存储找到的帧ID
    int flag = 0;   // 标记页面是否已在缓冲池中（1表示在，0表示不在）

    // 步骤1: 检查页面是否已在缓冲池中
    if(this->page_table_.find(page_id) != this->page_table_.end()) { //是否在缓冲池
        // 页面已在缓冲池中，直接获取其帧ID
        id = this->page_table_[page_id];
        flag = 1;  // 标记为已在缓冲池
    }
    else {
        // 步骤2: 页面不在缓冲池中，需要寻找牺牲页面
        if(!this->find_victim_page(&id)) {  //找空闲帧或替换
            return nullptr;  // 无法找到可用的帧，返回失败
        }
        // 步骤3: 更新牺牲页面（写回脏页，重置数据，更新页表）
        this->update_page(&this->pages_[id], page_id, id);
        // 步骤4: 从磁盘读取目标页面数据到缓冲池帧中
        this->disk_manager_->read_page(page_id.fd, page_id.page_no, this->pages_[id].get_data(), PAGE_SIZE);
    }

    // 步骤5: 固定页面，防止被替换
    this->replacer_->pin(id);  // 通知替换器此帧已被固定

    // 步骤6: 更新引脚计数
    if (flag == 1){
        // 页面已在缓冲池中，增加引脚计数
        this->pages_[id].pin_count_++;
    }
    else{
        // 新加载的页面，设置引脚计数为1
        this->pages_[id].pin_count_ = 1;
    }

    return &this->pages_[id]; //返回时自动解锁
}

/**
 * @description: 取消固定pin_count>0的在缓冲池中的page
 * @return {bool} 如果目标页的pin_count<=0则返回false，否则返回true
 * @param {PageId} page_id 目标page的page_id
 * @param {bool} is_dirty 若目标page应该被标记为dirty则为true，否则为false
 */
bool BufferPoolManager::unpin_page(PageId page_id, bool is_dirty) {
    // Todo:
    // 0. lock latch
    // 1. 尝试在page_table_中搜寻page_id对应的页P
    // 1.1 P在页表中不存在 return false
    // 1.2 P在页表中存在，获取其pin_count_
    // 2.1 若pin_count_已经等于0，则返回false
    // 2.2 若pin_count_大于0，则pin_count_自减一
    // 2.2.1 若自减后等于0，则调用replacer_的Unpin
    // 3 根据参数is_dirty，更改P的is_dirty_

    // 步骤0: 加锁保护并发访问
    std::scoped_lock lock{latch_};

    // 步骤1: 在页表中查找目标页面
    if(this->page_table_.find(page_id) == this->page_table_.end()) {  //不存在
        return false;  // 页面不在缓冲池中，无法解锁
    }

    // 步骤2: 获取页面对应的帧ID和页面对象
    frame_id_t id = this->page_table_[page_id]; //获取id
    Page* page = &this->pages_[id]; //通过id获取page

    // 步骤3: 检查引脚计数是否有效
    if(page->pin_count_ <= 0) {
        return false;  // 引脚计数已为0或无效，无法进一步解锁
    }
    else{
        // 步骤4: 减少引脚计数
        page->pin_count_ -= 1;
    }

    // 步骤5: 如果引脚计数变为0，通知替换器该页面可以被替换
    if(page->pin_count_ == 0) {
        this->replacer_->unpin(id);  // 允许替换器考虑此帧用于替换
    }

    // 步骤6: 根据参数设置脏页标记
    if (is_dirty){
        page->is_dirty_ = is_dirty;  // 标记页面为脏页，需要写回磁盘
    }

    return true;  // 成功解锁
}

/**
 * @description: 将目标页写回磁盘，不考虑当前页面是否正在被使用
 * @return {bool} 成功则返回true，否则返回false(只有page_table_中没有目标页时)
 * @param {PageId} page_id 目标页的page_id，不能为INVALID_PAGE_ID
 */
bool BufferPoolManager::flush_page(PageId page_id) {
    // Todo:
    // 0. lock latch
    // 1. 查找页表,尝试获取目标页P
    // 1.1 目标页P没有被page_table_记录 ，返回false
    // 2. 无论P是否为脏都将其写回磁盘。
    // 3. 更新P的is_dirty_
   
    // 步骤0: 加锁保护并发访问
    std::scoped_lock lock{latch_};

    // 步骤1: 在页表中查找目标页面
    if(this->page_table_.find(page_id) == this->page_table_.end()) {
        return false;  // 页面不在缓冲池中，无法刷新
    }

    // 步骤2: 获取页面对应的帧ID和页面对象
    frame_id_t id = this->page_table_[page_id]; //获取id
    Page* page = &this->pages_[id]; //通过id获取page

    // 步骤3: 将页面数据写回磁盘，无论是否为脏页
    this->disk_manager_->write_page(page->get_page_id().fd, page->get_page_id().page_no, page->get_data(), PAGE_SIZE);
    page->is_dirty_ = false;  // 写回后清除脏位标记

    return true;  // 成功刷新页面
}

/**
 * @description: 创建一个新的page，即从磁盘中移动一个新建的空page到缓冲池某个位置。
 * @return {Page*} 返回新创建的page，若创建失败则返回nullptr
 * @param {PageId*} page_id 当成功创建一个新的page时存储其page_id
 */
Page* BufferPoolManager::new_page(PageId* page_id) {
    // 1.   获得一个可用的frame，若无法获得则返回nullptr
    // 2.   在fd对应的文件分配一个新的page_id
    // 3.   将frame的数据写回磁盘
    // 4.   固定frame，更新pin_count_
    // 5.   返回获得的page

    // 步骤0: 加锁保护并发访问
    std::scoped_lock lock{latch_};

    frame_id_t id;  // 用于存储分配的帧ID

    // 步骤1: 寻找可用的帧（空闲帧或通过替换获得）
    if(this->find_victim_page(&id)) {  //找到一个位置
        // 步骤2: 为指定文件分配新的页面编号
        page_id->page_no = this->disk_manager_->allocate_page(page_id->fd); //获取编号

        // 步骤3: 更新帧以存放新页面（写回脏数据，重置内存，更新页表）
        this->update_page(&this->pages_[id], *page_id, id);  //更新page

        // 步骤4: 固定新页面，防止被替换
        this->replacer_->pin(id);  // 通知替换器此帧已被固定
        this->pages_[id].pin_count_ = 1;  // 设置引脚计数为1

    } else {
        return nullptr;  // 无法找到可用帧，创建失败
    }

    return &this->pages_[id];  // 返回新创建的页面
}

/**
 * @description: 从buffer_pool删除目标页
 * @return {bool} 如果目标页不存在于buffer_pool或者成功被删除则返回true，若其存在于buffer_pool但无法删除则返回false
 * @param {PageId} page_id 目标页
 */
bool BufferPoolManager::delete_page(PageId page_id) {
    // 1.   在page_table_中查找目标页，若不存在返回true
    // 2.   若目标页的pin_count不为0，则返回false
    // 3.   将目标页数据写回磁盘，从页表中删除目标页，重置其元数据，将其加入free_list_，返回true
    
    // 步骤0: 加锁保护并发访问
    std::scoped_lock lock{latch_};

    // 步骤1: 检查页面是否存在于缓冲池中
    if(this->page_table_.find(page_id) == this->page_table_.end()) {
        return true;  // 页面不在缓冲池中，视为删除成功
    }

    // 步骤2: 获取页面对应的帧ID和页面对象
    frame_id_t id = this->page_table_[page_id];
    Page* page = &this->pages_[id];

    // 步骤3: 检查页面是否正在被使用（引脚计数不为0）
    if(page->pin_count_ != 0) {  //还在被使用，不能删除
        return false;  // 页面被固定，无法删除
    }

    // 步骤4: 释放磁盘上的页面空间
    this->disk_manager_->deallocate_page(page->get_page_id().page_no);

    // 步骤5: 标记页面ID为无效，并更新页面状态
    page_id.page_no = INVALID_PAGE_ID;
    this->update_page(page, page_id, id); //包含page table处理

    // 步骤6: 将帧重新加入空闲列表，供后续使用
    this->free_list_.push_back(id);

    return true;  // 成功删除页面
}

/**
 * @description: 将buffer_pool中的所有页写回到磁盘
 * @param {int} fd 文件句柄
 */
void BufferPoolManager::flush_all_pages(int fd) {
    // 步骤0: 加锁保护并发访问
    std::scoped_lock lock{latch_};

    // 步骤1: 遍历所有缓冲池帧
    for (size_t i = 0; i < pool_size_; i++) {
        Page *page = &this->pages_[i];

        // 步骤2: 检查页面是否属于指定文件且为有效页面
        if (page->get_page_id().fd == fd && page->get_page_id().page_no != INVALID_PAGE_ID) {
            // 步骤3: 将页面数据写回磁盘
            disk_manager_->write_page(page->get_page_id().fd, page->get_page_id().page_no, page->get_data(), PAGE_SIZE);
            page->is_dirty_ = false;  // 清除脏位标记
        }
    }
}