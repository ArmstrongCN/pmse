/*
 * Copyright 2014-2016, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * pmstore_list_int_ptr.cpp
 *
 *  Created on: Sep 22, 2016
 *      Author: kfilipek
 */

#include "pmse_list_int_ptr.h"

#include <exception>
#include <iostream>

namespace mongo {

PmseListIntPtr::PmseListIntPtr() : counter(1) {
    pop = pool_by_vptr(this);
};

PmseListIntPtr::~PmseListIntPtr() {
}

void PmseListIntPtr::setPool() {
    pop = pool_by_vptr(this);
}

uint64_t PmseListIntPtr::size() {
    return _size;
}

void PmseListIntPtr::insertKV(persistent_ptr<KVPair> &key, persistent_ptr<InitData> &value) {
    try {
        transaction::exec_tx(pop, [&] {
            key->ptr = value;
            key->next = nullptr;
            if (head != nullptr) {
                tail->next = key;
                tail = key;
            } else {
                head = key;
                tail = head;
            }
            _size++;
        });
    } catch (std::exception &e) {
        std::cout << "KVMapper: " << e.what() << std::endl;
    }
}

void PmseListIntPtr::insertKV_capped(persistent_ptr<KVPair> &key,
                                     persistent_ptr<InitData> &value,
                                     bool isCapped, uint64_t maxDoc,
                                     uint64_t sizeOfColl) {
    try {
        transaction::exec_tx(pop, [&] {
            key->ptr = value;
            key->next = nullptr;

            isFullCapped = false;
            size_t value_size = pmemobj_alloc_usable_size(value.raw());
            uint64_t tempSize = actualSizeOfCollecion + value_size;

            if(tempSize >= sizeOfColl) {
                if((tempSize - (pmemobj_alloc_usable_size(head.raw()) + sizeOfFirstData)) > sizeOfColl)
                    isSpace = BLOCKED;
                else
                    isSpace = NO;
            } else {
                isSpace = YES;
            }

            if(head != nullptr) {
                if(_size == maxDoc || isSpace == NO) {
                    if(head->next != nullptr) {
                        head = head->next;
                        tail->next = key;
                        tail = key;
                    }
                    else {
                        head = key;
                        tail = head;
                    }

                    actualSizeOfCollecion -= (sizeOfFirstData + pmemobj_alloc_usable_size(head.raw()))
                                     + value_size;

                    if(!isFullCapped)
                        isFullCapped = true;

                    delete_persistent<KVPair>(first);
                    first = head;
                    sizeOfFirstData = value_size;
                } else if(isSpace == YES) {
                    tail->next = key;
                    tail = key;
                    actualSizeOfCollecion = tempSize;
                }
            } else if(head == nullptr) {
                head = key;
                tail = head;
                first = head;
                sizeOfFirstData = value_size;
                actualSizeOfCollecion = tempSize;
            }

            if(!isFullCapped)
                _size++;
        });
    } catch (std::exception &e) {
        std::cout << "KVMapper: " << e.what() << std::endl;
    }
}

int64_t PmseListIntPtr::deleteKV(uint64_t key, persistent_ptr<KVPair> &deleted) {
    auto before = head;
    int64_t sizeFreed = 0;
    for (auto rec = head; rec != nullptr; rec = rec->next) {
        if (rec->idValue == key) {
            transaction::exec_tx(pop, [&] {
                if (before != head) {
                    before->next = rec->next;
                    if (before->next == nullptr)
                        tail = before;
                    before.flush();
                } else {
                    if(head == rec) {
                        head = rec->next;
                    } else {
                        before->next = rec->next;
                        if(rec->next != nullptr) {
                            tail = rec->next;
                        } else {
                            tail = before;
                        }
                    }
                    if(head == nullptr) {
                        tail = head;
                    }
                }
                _size--;
                if(deleted != nullptr) {
                    rec->next = deleted;
                    deleted = rec;
                } else {
                    rec->next = nullptr;
                    deleted = rec;
                }
                sizeFreed = pmemobj_alloc_usable_size(deleted->ptr.raw());
                delete_persistent<InitData>(deleted->ptr);
            });
            break;
        } else {
            before = rec;
        }
    }
    return sizeFreed;
}

bool PmseListIntPtr::hasKey(uint64_t key) {
    for (auto rec = head; rec != nullptr; rec = rec->next) {
        if (rec->idValue == key) {
            return true;
        }
    }
    return false;
}

bool PmseListIntPtr::find(uint64_t key, persistent_ptr<InitData> &item_ptr) {
    for (auto rec = head; rec != nullptr; rec = rec->next) {
        if (rec->idValue == key) {
            item_ptr = rec->ptr;
            return true;
        }
    }
    item_ptr = nullptr;
    return false;
}

bool PmseListIntPtr::getPair(uint64_t key, persistent_ptr<KVPair> &item_ptr) {
    for (auto rec = head; rec != nullptr; rec = rec->next) {
        if (rec->idValue == key) {
            item_ptr = rec;
            return true;
        }
    }
    item_ptr = nullptr;
    return false;
}

void PmseListIntPtr::update(uint64_t key, persistent_ptr<InitData> &value) {
    for (auto rec = head; rec != nullptr; rec = rec->next) {
        if (rec->idValue == key) {
            if(rec->ptr != nullptr) {
                try {
                    transaction::exec_tx(pop, [&] {
                        delete_persistent<InitData>(rec->ptr);
                    });
                } catch(std::exception &e) {
                    std::cout << e.what() << std::endl;
                }
            }
            rec->ptr = value;
            return;
        }
    }
}

void PmseListIntPtr::clear() {
    if (!head)
        return;
    transaction::exec_tx(pop, [&] {
        for(auto rec = head; rec != nullptr;) {
            auto temp = rec->next;
            delete_persistent<KVPair>(rec);
            rec = temp;
        }
        head = nullptr;
        _size = 0;
    });
}

uint64_t PmseListIntPtr::getNextId() {
    return counter++;
}

}
