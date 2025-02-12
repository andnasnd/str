/*
*/
	#include <stdint.h>
	#include <ctype.h>
	#include <limits.h>
	#include "str.h"
	#include "strbuf.h"

	#ifdef STRBUF_PROVIDE_PRINTF
		#include <stdio.h>
	#endif

	#ifdef STRBUF_PROVIDE_PRNF
		#include "prnf.h"
	#endif

	#ifdef STRBUF_DEFAULT_ALLOCATOR_STDLIB
		#include <stdlib.h>
	#endif

	#ifdef STRBUF_ASSERT_DEFAULT_ALLOCATOR_STDLIB
		#include <assert.h>
	#endif

//********************************************************************************************************
// Local defines
//********************************************************************************************************

//	#include <stdio.h>
//	#define DBG(_fmtarg, ...) printf("%s:%.4i - "_fmtarg"\n" , __FILE__, __LINE__ ,##__VA_ARGS__)

//********************************************************************************************************
// Private prototypes
//********************************************************************************************************

	static strbuf_t* create_buf(int initial_capacity, strbuf_allocator_t allocator);
	static str_t buffer_vcat(strbuf_t** buf_ptr, int n_args, va_list va);
	static void insert_str_into_buf(strbuf_t** buf_ptr, int index, str_t str);
	static void destroy_buf(strbuf_t** buf_ptr);
	static void change_buf_capacity(strbuf_t** buf_ptr, int new_capacity);
	static void assign_str_to_buf(strbuf_t** buf_ptr, str_t str);
	static void append_char_to_buf(strbuf_t** strbuf, char c);
	static int  round_up_capacity(int capacity);
	static str_t str_of_buf(strbuf_t* buf);
	static bool buf_contains_str(strbuf_t* buf, str_t str);
	static bool buf_is_dynamic(strbuf_t* buf);
	static void empty_buf(strbuf_t* buf);
	static bool add_will_overflow_int(int a, int b);

#ifdef STRBUF_PROVIDE_PRNF
	static void char_handler_for_prnf(void* dst, char c);
#endif

#ifdef STRBUF_DEFAULT_ALLOCATOR_STDLIB
	static void* allocfunc_stdlib(struct strbuf_allocator_t* this_allocator, void* ptr_to_free, size_t size, const char* caller_filename, int caller_line);
	static strbuf_allocator_t default_allocator = (strbuf_allocator_t){.allocator=allocfunc_stdlib, .app_data=NULL};
#endif

//********************************************************************************************************
// Public functions
//********************************************************************************************************

#ifdef STRBUF_DEFAULT_ALLOCATOR_STDLIB
static void* allocfunc_stdlib(struct strbuf_allocator_t* this_allocator, void* ptr_to_free, size_t size, const char* caller_filename, int caller_line)
{
	(void)this_allocator; (void)caller_filename; (void)caller_line;
	void* result;
	result = realloc(ptr_to_free, size);
	#ifdef STRBUF_ASSERT_DEFAULT_ALLOCATOR_STDLIB
		assert(size==0 || result);
	#endif
	return result;
}
#endif

strbuf_t* strbuf_create(size_t initial_capacity, strbuf_allocator_t* allocator)
{
	strbuf_t* result;
	
	#ifdef STRBUF_DEFAULT_ALLOCATOR_STDLIB
	if(!allocator)
		allocator = &default_allocator;
	#endif

	if(allocator && allocator->allocator && initial_capacity <= INT_MAX)
		result = create_buf((int)initial_capacity, *allocator);
	else
		result = NULL;
	return result;
}

strbuf_t* strbuf_create_fixed(void* addr, size_t addr_size)
{
	strbuf_t* result = NULL;
	intptr_t alignment_mask;
	size_t capacity;

	if(addr_size > sizeof(strbuf_t) && addr)
	{
		capacity = addr_size - sizeof(strbuf_t) - 1;
		//check alignment
		alignment_mask = sizeof(void*)-1;
		alignment_mask &= (intptr_t)addr;
		if(alignment_mask == 0)
		{
			result = addr;
			result->allocator.app_data = NULL;
			result->allocator.allocator = NULL;
			result->capacity =  capacity <= INT_MAX ? capacity:INT_MAX;
			result->size = 0;
			empty_buf(result);
		};
	};

	return result;
}

// concatenate a number of str's this can include the buffer itself, strbuf.str for appending
str_t _strbuf_cat(strbuf_t** buf_ptr, int n_args, ...)
{
	va_list va;
	va_start(va, n_args);
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
		str = buffer_vcat(buf_ptr, n_args, va);
	va_end(va);
	return str;
}

str_t strbuf_vcat(strbuf_t** buf_ptr, int n_args, va_list va)
{
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
		str = buffer_vcat(buf_ptr, n_args, va);
	return str;
}

#ifdef STRBUF_PROVIDE_PRINTF
str_t strbuf_printf(strbuf_t** buf_ptr, const char* format, ...)
{
	va_list va;
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
	{
		va_start(va, format);
		str = strbuf_vprintf(buf_ptr, format, va);
		va_end(va);
	};
	return str;
}

str_t strbuf_vprintf(strbuf_t** buf_ptr, const char* format, va_list va)
{
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
	{
		empty_buf(*buf_ptr);
		str = strbuf_append_vprintf(buf_ptr, format, va);
	};
	return str;
}

str_t strbuf_append_printf(strbuf_t** buf_ptr, const char* format, ...)
{
	va_list va;
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
	{
		va_start(va, format);
		str = strbuf_append_vprintf(buf_ptr, format, va);
		va_end(va);
	};
	return str;
}

	
str_t strbuf_append_vprintf(strbuf_t** buf_ptr, const char* format, va_list va)
{
	int size;
	int append_size;
	bool failed;
	strbuf_t* buf;
	str_t str = {0};
	va_list vb;
	if(buf_ptr && *buf_ptr)
	{
		va_copy(vb, va);
		buf = *buf_ptr;
		size = buf->size;
		append_size = vsnprintf(NULL, 0, format, va);

		failed = add_will_overflow_int(size, append_size);
		if(!failed)
		{
			size += append_size;
			if(buf_is_dynamic(buf) && size > buf->capacity)
				change_buf_capacity(&buf, round_up_capacity(size));

			failed = size > buf->capacity;
		};

		if(!failed)
			buf->size += vsnprintf(&buf->cstr[buf->size], buf->capacity - buf->size, format, vb);
		else
			empty_buf(buf);

		str = strbuf_str(&buf);
		*buf_ptr = buf;
		va_end(vb);
	};
	return str;
}
#endif

#ifdef STRBUF_PROVIDE_PRNF
str_t strbuf_prnf(strbuf_t** buf_ptr, const char* format, ...)
{
	va_list va;
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
	{
		va_start(va, format);
		str = strbuf_vprnf(buf_ptr, format, va);
		va_end(va);;
	};
	return str;
}

str_t strbuf_vprnf(strbuf_t** buf_ptr, const char* format, va_list va)
{
	strbuf_t* buf;
	str_t str = {0};
	int char_count;
	if(buf_ptr && *buf_ptr)
	{
		buf = *buf_ptr;
		empty_buf(buf);

		char_count = vfptrprnf(char_handler_for_prnf, &buf,  format, va);

		if(char_count > buf->size)
			empty_buf(buf);

		str = strbuf_str(&buf);
		*buf_ptr = buf;
	};
	return str;
}

str_t strbuf_append_prnf(strbuf_t** buf_ptr, const char* format, ...)
{
	va_list va;
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
	{
		va_start(va, format);
		str = strbuf_append_vprnf(buf_ptr, format, va);
		va_end(va);;
	};
	return str;
}

str_t strbuf_append_vprnf(strbuf_t** buf_ptr, const char* format, va_list va)
{
	strbuf_t* buf;
	str_t str = {0};
	int char_count;
	if(buf_ptr && *buf_ptr)
	{
		buf = *buf_ptr;

		char_count = buf->size;
		char_count += vfptrprnf(char_handler_for_prnf, &buf,  format, va);

		if(char_count > buf->size || char_count < 0)
			empty_buf(buf);

		str = strbuf_str(&buf);
		*buf_ptr = buf;
	};
	return str;
}

#endif

str_t strbuf_str(strbuf_t** buf_ptr)
{
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
		str = str_of_buf(*buf_ptr);
	return str;
}

str_t strbuf_append_char(strbuf_t** buf_ptr, char c)
{
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
	{
		append_char_to_buf(buf_ptr, c);
		str = str_of_buf(*buf_ptr);
	};
	return str;
}

// reduce allocation size to the minimum possible
str_t strbuf_shrink(strbuf_t** buf_ptr)
{
	str_t str = {0};
	if(buf_ptr && *buf_ptr)
	{
		if(buf_is_dynamic(*buf_ptr))
			change_buf_capacity(buf_ptr, (*buf_ptr)->size);
		str = str_of_buf(*buf_ptr);
	};
	return str;
}

void strbuf_destroy(strbuf_t** buf_ptr)
{
	if(buf_ptr)
	{
		if(*buf_ptr && buf_is_dynamic(*buf_ptr))
			destroy_buf(buf_ptr);
		*buf_ptr = NULL;
	};	
}

str_t strbuf_assign(strbuf_t** buf_ptr, str_t str)
{
	strbuf_t* buf;
	bool failed;
	if(buf_ptr && *buf_ptr)
	{
		buf = *buf_ptr;
		failed = !str_is_valid(str);
		if(!failed)
		{
			if(str.size > buf->capacity && buf_is_dynamic(buf))
				change_buf_capacity(&buf, round_up_capacity(str.size));
			
			failed = str.size > buf->capacity;
		};
		if(!failed)
		{
			memmove(buf->cstr, str.data, (size_t)str.size);
			buf->size = str.size;
			buf->cstr[buf->size] = 0;
		}
		else
			empty_buf(buf);
		*buf_ptr = buf;
	};

	return str_of_buf(buf);
}

str_t strbuf_append(strbuf_t** buf_ptr, str_t str)
{
	if(buf_ptr && *buf_ptr)
		insert_str_into_buf(buf_ptr, (*buf_ptr)->size, str);
	return str_of_buf(*buf_ptr);
}

str_t strbuf_prepend(strbuf_t** buf_ptr, str_t str)
{
	if(buf_ptr && *buf_ptr)
		insert_str_into_buf(buf_ptr, 0, str);
	return str_of_buf(*buf_ptr);
}

str_t strbuf_insert_at_index(strbuf_t** buf_ptr, int index, str_t str)
{
	if(buf_ptr && *buf_ptr)
		insert_str_into_buf(buf_ptr, index, str);
	return str_of_buf(*buf_ptr);
}

str_t strbuf_insert_before(strbuf_t** buf_ptr, str_t dst, str_t src)
{
	strbuf_t* buf;

	if(buf_ptr && *buf_ptr)
	{
		buf = *buf_ptr;
		if(buf->cstr <= dst.data && dst.data <= &buf->cstr[buf->size])
			insert_str_into_buf(&buf, dst.data - buf->cstr, src);
		*buf_ptr = buf;
	};

	return str_of_buf(*buf_ptr);
}

str_t strbuf_insert_after(strbuf_t** buf_ptr, str_t dst, str_t src)
{
	strbuf_t* buf;
	const char* dst_ptr;

	if(buf_ptr && *buf_ptr && str_is_valid(dst))
	{
		buf = *buf_ptr;
		dst_ptr = &dst.data[dst.size];

		if(buf->cstr <= dst_ptr && dst_ptr <= &buf->cstr[buf->size])
			insert_str_into_buf(&buf, dst_ptr - buf->cstr, src);
		*buf_ptr = buf;
	};

	return str_of_buf(*buf_ptr);
}

//********************************************************************************************************
// Private functions
//********************************************************************************************************

static strbuf_t* create_buf(int initial_capacity, strbuf_allocator_t allocator)
{
	strbuf_t* buf = NULL;

	if(initial_capacity <= INT_MAX)
	{
		buf = allocator.allocator(&allocator, NULL, sizeof(strbuf_t)+initial_capacity+1,  __FILE__, __LINE__);
		buf->capacity = initial_capacity;
		buf->allocator = allocator;
		empty_buf(buf);
	};

	return buf;
}

static str_t str_of_buf(strbuf_t* buf)
{
	str_t str = {.data = NULL, .size = 0};
	if(buf)
	{
		str.data = buf->cstr;
		str.size = buf->size;
	};
	return str;
}

static str_t buffer_vcat(strbuf_t** buf_ptr, int n_args, va_list va)
{
	str_t 	str;
	int 	size_needed = 0;
	bool	tmp_buf_needed = false;
	int 	i = 0;
	bool 	failed = false;
	strbuf_t* dst_buf = *buf_ptr;
	strbuf_t* build_buf;
	va_list vb;
	va_copy(vb, va);

	while(i++ != n_args)
	{
		str = va_arg(va, str_t);
		failed |= add_will_overflow_int(size_needed, str.size);
		size_needed += str.size;
		tmp_buf_needed |= buf_contains_str(dst_buf, str);
	};

	if(!failed)
	{
		if(tmp_buf_needed && buf_is_dynamic(dst_buf))
			build_buf = create_buf(size_needed, dst_buf->allocator);
		else
		{
			if(buf_is_dynamic(dst_buf) && dst_buf->capacity < size_needed)
				change_buf_capacity(&dst_buf, round_up_capacity(size_needed));
			build_buf = dst_buf;
			empty_buf(build_buf);
		};

		if(buf_is_dynamic(build_buf) || !tmp_buf_needed)
		{
			if(build_buf->capacity >= size_needed)
			{
				i = 0;
				while(i++ != n_args)
					insert_str_into_buf(&build_buf, build_buf->size, va_arg(vb, str_t));	
			};
		};

		if(tmp_buf_needed && buf_is_dynamic(build_buf))
		{
			assign_str_to_buf(&dst_buf, str_of_buf(build_buf));
			destroy_buf(&build_buf);
		};
	}
	else
		empty_buf(build_buf);

	*buf_ptr = dst_buf;

	va_end(vb);
	return str_of_buf(dst_buf);
}

static void insert_str_into_buf(strbuf_t** buf_ptr, int index, str_t str)
{
	strbuf_t* buf = *buf_ptr;
	bool src_in_dst = buf_contains_str(buf, str);
	size_t src_offset = str.data - buf->cstr;
	str_t str_part_left_behind = {.data=NULL, .size=0};
	str_t str_part_shifted;
	char* move_src;
	char* move_dst;
	bool failed;

	if(index > buf->size)
		index = buf->size;
	if(index < 0)
		index += buf->size;
	if(index < 0)
		index = 0;

	failed = add_will_overflow_int(buf->size, str.size);

	if(!failed)
	{
		if(buf_is_dynamic(buf) && buf->capacity < buf->size + str.size)
			change_buf_capacity(&buf, round_up_capacity(buf->size + str.size));

		if(src_in_dst && buf != *buf_ptr)
			str.data = buf->cstr + src_offset;

		failed = buf->capacity < buf->size + str.size;
	};

	if(!failed)
	{
		str_part_shifted = str;
		move_src = &buf->cstr[index];
		move_dst = &buf->cstr[index+str.size];
		if(str.size)
		{
			memmove(move_dst, move_src, buf->size-index);
			if(src_in_dst)
			{
				if(move_src > str.data)
					str_part_left_behind = str_pop_split(&str_part_shifted, move_src - str.data);
				str_part_shifted.data += move_dst-move_src;
			};
		};

		buf->size += str.size;
		if(str_part_left_behind.size)
			memcpy(move_src, str_part_left_behind.data, str_part_left_behind.size);
		move_src += str_part_left_behind.size;
		if(str_part_shifted.size)
			memcpy(move_src, str_part_shifted.data, str_part_shifted.size);
		buf->cstr[buf->size] = 0;
	}
	else
		empty_buf(buf);

	*buf_ptr = buf;
}

static void destroy_buf(strbuf_t** buf_ptr)
{
	strbuf_t* buf = *buf_ptr;
	if(buf_is_dynamic(buf))
		buf->allocator.allocator(&buf->allocator, buf, 0, __FILE__, __LINE__);
	*buf_ptr = NULL;
}

static void change_buf_capacity(strbuf_t** buf_ptr, int new_capacity)
{
	strbuf_t* buf = *buf_ptr;

	if(buf_is_dynamic(buf))
	{
		if(new_capacity < buf->size)
			new_capacity = buf->size;

		if(new_capacity != buf->capacity)
		{
			buf = buf->allocator.allocator(&buf->allocator, buf, sizeof(strbuf_t)+new_capacity+1, __FILE__, __LINE__);
			buf->capacity = new_capacity;
		};
	};
	*buf_ptr = buf;
}

static void assign_str_to_buf(strbuf_t** buf_ptr, str_t str)
{
	empty_buf(*buf_ptr);
	insert_str_into_buf(buf_ptr, 0, str);
}

static void append_char_to_buf(strbuf_t** buf_ptr, char c)
{
	strbuf_t* buf = *buf_ptr;
	bool failed = add_will_overflow_int(buf->size, 1);

	if(!failed)
	{
		if(buf_is_dynamic(buf) && buf->size+1 > buf->capacity)
			change_buf_capacity(&buf, round_up_capacity(buf->size + 1));
		failed = buf->capacity < buf->size+1;
	};

	if(!failed)
	{
		buf->cstr[buf->size] = c;
		buf->size++;
		buf->cstr[buf->size] = 0;
	}
	else
		empty_buf(buf);

	*buf_ptr = buf;
}

static int round_up_capacity(int capacity)
{
	int remainder = capacity % STRBUF_CAPACITY_GROW_STEP;
	
	if(remainder)
	{
		if(!add_will_overflow_int(capacity, STRBUF_CAPACITY_GROW_STEP-remainder))
			capacity += STRBUF_CAPACITY_GROW_STEP-remainder;
		else
			capacity = INT_MAX;
	};

	return capacity;
}

static bool buf_contains_str(strbuf_t* buf, str_t str)
{
	return &buf->cstr[0] <= str.data && str.data < &buf->cstr[buf->size];
}

static bool buf_is_dynamic(strbuf_t* buf)
{
	return !!(buf->allocator.allocator);
}

static void empty_buf(strbuf_t* buf)
{
	buf->size = 0;
	buf->cstr[0] = 0;
}

static bool add_will_overflow_int(int a, int b)
{
	int c = a;
	c += b;
	return ((a < 0) == (b < 0) && (a < 0) != (c < 0));
}

#ifdef STRBUF_PROVIDE_PRNF
static void char_handler_for_prnf(void* dst, char c)
{
	append_char_to_buf((strbuf_t**)dst, c);
}
#endif
