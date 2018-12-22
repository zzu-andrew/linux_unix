/***
 * Excerpted from "Test-Driven Development for Embedded C",
 * published by The Pragmatic Bookshelf.
 * Copyrights apply to this code. It may not be used to create training material, 
 * courses, books, articles, and the like. Contact us if you are in doubt.
 * We make no guarantees that this code is fit for any purpose. 
 * Visit http://www.pragmaticprogrammer.com/titles/jgade for more book information.
***/
#ifndef D_ClassName_H
#define D_ClassName_H

///////////////////////////////////////////////////////////////////////////////
//
//  ClassName is responsible for ...
//
///////////////////////////////////////////////////////////////////////////////

typedef struct _ClassName Classname;

ClassName* ClassName_Create(void);
void ClassName_Destroy(ClassName*);
void ClassName_VirtualFunction_impl(ClassName*);

#endif  // D_ClassName_H
