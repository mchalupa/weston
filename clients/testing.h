#ifndef _TOYTOOLKIT_CLIENTS_TESTING_H_
#define _TOYTOOLKIT_CLIENTS_TESTING_H_

#define TESTING_GETTER(name, type, field)				\
	void *name##_get_##field(type *ptr);				\
	void *name##_get_##field(type *ptr) { return ptr->field; }

#endif /* _TOYTOOLKIT_CLIENTS_TESTING_H_ */
