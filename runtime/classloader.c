/*
 * Copyright (c) 2009 Tomasz Grabiec
 *
 * This file is released under the GPL version 2 with the following
 * clarification and special exception:
 *
 *     Linking this library statically or dynamically with other modules is
 *     making a combined work based on this library. Thus, the terms and
 *     conditions of the GNU General Public License cover the whole
 *     combination.
 *
 *     As a special exception, the copyright holders of this library give you
 *     permission to link this library with independent modules to produce an
 *     executable, regardless of the license terms of these independent
 *     modules, and to copy and distribute the resulting executable under terms
 *     of your choice, provided that you also meet, for each linked independent
 *     module, the terms and conditions of the license of that module. An
 *     independent module is a module which is not derived from or based on
 *     this library. If you modify this library, you may extend this exception
 *     to your version of the library, but you are not obligated to do so. If
 *     you do not wish to do so, delete this exception statement from your
 *     version.
 *
 * Please refer to the file LICENSE for details.
 */

#include "jit/exception.h"

#include "runtime/classloader.h"

#include "vm/class.h"
#include "vm/object.h"
#include "vm/classloader.h"

#include <stdlib.h>
#include <stdio.h>

jobject native_vmclassloader_getprimitiveclass(jint type)
{
	char primitive_class_name[] = { "X" };
	struct vm_class *class;

	primitive_class_name[0] = (char)type;

	class = classloader_load_primitive(primitive_class_name);
	if (!class)
		return NULL;

	vm_class_ensure_init(class);
	if (exception_occurred())
		return NULL;

	return class->object;
}

jobject native_vmclassloader_findloadedclass(jobject classloader, jobject name)
{
	struct vm_class *vmc;
	char *c_name;

	c_name = vm_string_to_cstr(name);
	if (!c_name)
		return NULL;

	vmc = classloader_find_class(c_name);
	free(c_name);

	if (!vmc)
		return NULL;

	if (vmc->classloader != classloader)
		return NULL;

	vm_class_ensure_init(vmc);
	return vmc->object;
}

/* TODO: respect the @resolve parameter. */
jobject native_vmclassloader_loadclass(jobject name, jboolean resolve)
{
	struct vm_class *vmc;
	char *c_name;

	c_name = vm_string_to_cstr(name);
	if (!c_name)
		return NULL;

	vmc = classloader_load(NULL, c_name);
	free(c_name);
	if (!vmc)
		return NULL;

	if (vm_class_ensure_object(vmc))
		return NULL;

	return vmc->object;
}