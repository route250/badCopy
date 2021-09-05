/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   debug.h
 * Author: maeda
 *
 * Created on 2021年8月17日, 22:18
 */

#ifndef DEBUG_H
#define DEBUG_H

extern int log_level;

#define dbg1_printf(fmt, ...)  printf(fmt, __VA_ARGS__)
#define err1_printf(fmt, ...)  printf(fmt, __VA_ARGS__)

#endif /* DEBUG_H */

