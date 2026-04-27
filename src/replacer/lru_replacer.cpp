/* Copyright (c) 2023 Renmin University of China
RMDB is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
        http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "lru_replacer.h"

LRUReplacer::LRUReplacer(size_t num_pages) { max_size_ = num_pages; }

LRUReplacer::~LRUReplacer() = default;  

/**
 * @description: 使用LRU策略删除一个victim frame，并返回该frame的id
 * @param {frame_id_t*} frame_id 被移除的frame的id，如果没有frame被移除返回nullptr
 * @return {bool} 如果成功淘汰了一个页面则返回true，否则返回false
 */
bool LRUReplacer::victim(frame_id_t* frame_id) {
    // Todo:
    //  利用lru_replacer中的LRUlist_,LRUHash_实现LRU策略
    //  选择合适的frame指定为淘汰页面,赋值给*frame_id

    // 使用std::scoped_lock进行线程安全的上锁操作
    std::scoped_lock lock{latch_};

    // 检查LRUlist是否为空，如果为空则没有可淘汰的页面
    if (LRUlist_.empty()) {
        frame_id = nullptr;  // 设置为nullptr表示没有淘汰页面
        return false;        // 返回false表示淘汰失败
    } else {
        // 选择LRUlist的尾部元素（最少最近使用的页面）
        *frame_id = LRUlist_.back();
        // 从LRUlist中移除该元素
        LRUlist_.pop_back();
        // 从LRUhash中移除对应的映射
        LRUhash_.erase(*frame_id);
        // 返回true表示成功淘汰了一个页面
        return true;
    }

    // 注意：这里的return true是多余的，因为else分支已经返回了，但为了完整性保留
    return true;
}

/**
 * @description: 固定指定的frame，即该页面无法被淘汰
 * @param {frame_id_t} 需要固定的frame的id
 */
void LRUReplacer::pin(frame_id_t frame_id) {
    // Todo:
    // 固定指定id的frame
    // 在数据结构中移除该frame

    // 使用std::scoped_lock进行线程安全的上锁操作
    std::scoped_lock lock{latch_};

    // 在LRUhash中查找指定的frame_id
    auto find = LRUhash_.find(frame_id);
    // 如果找到该frame（即它在LRUlist中）
    if (find != LRUhash_.end()) {
        // 从LRUlist中移除该frame（使用迭代器进行高效删除）
        LRUlist_.erase(find->second);
        // 从LRUhash中移除对应的映射
        LRUhash_.erase(find);
    }
    // 如果没有找到，说明该frame不在replacer中，无需操作
}

/**
 * @description: 取消固定一个frame，代表该页面可以被淘汰
 * @param {frame_id_t} frame_id 取消固定的frame的id
 */
void LRUReplacer::unpin(frame_id_t frame_id) {
    // Todo:
    //  支持并发锁
    //  选择一个frame取消固定
    
    // 使用std::scoped_lock进行线程安全的上锁操作
    std::scoped_lock lock{latch_};

    // 在LRUhash中查找指定的frame_id
    auto find = LRUhash_.find(frame_id);
    // 如果没有找到该frame（即它不在LRUlist中）
    if (find == LRUhash_.end()) {
        // 将该frame添加到LRUlist的头部（表示最近被访问）
        LRUlist_.push_front(frame_id);
        // 在LRUhash中记录该frame的迭代器位置
        LRUhash_[frame_id] = LRUlist_.begin();
    }
    // 如果已经存在，无需操作（假设已经是最新的）
}

/**
 * @description: 获取当前replacer中可以被淘汰的页面数量
 */
size_t LRUReplacer::Size() { return LRUlist_.size(); }
