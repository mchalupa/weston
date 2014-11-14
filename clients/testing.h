#ifndef _TOYTOOLKIT_CLIENTS_TESTING_H_
#define _TOYTOOLKIT_CLIENTS_TESTING_H_

#ifdef TESTING
#define TESTING_EXPORT WL_EXPORT
#else
#define TESTING_EXPORT static
#endif

#define TESTING_GETTER(name, type, field)				\
	void *name##_get_##field(type *ptr);				\
	void *name##_get_##field(type *ptr) { return ptr->field; }

#endif /* _TOYTOOLKIT_CLIENTS_TESTING_H_ */
