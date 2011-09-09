#include <string.h>
#include <nullvm.h>

#define SOURCE_FILE 1
#define SIGNATURE 2
#define INNER_CLASS 3
#define ENCLOSING_METHOD 4
#define EXCEPTIONS 5
#define RUNTIME_VISIBLE_ANNOTATIONS 6
#define RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS 7
#define ANNOTATION_DEFAULT 8

static Class* java_lang_TypeNotPresentException = NULL;
static Class* java_lang_annotation_AnnotationFormatError = NULL;
static Class* java_lang_reflect_Method = NULL;
static Method* java_lang_reflect_Method_init = NULL;
static Class* org_apache_harmony_lang_annotation_AnnotationMember = NULL;
static Method* org_apache_harmony_lang_annotation_AnnotationMember_init = NULL;
static Class* org_apache_harmony_lang_annotation_AnnotationFactory = NULL;
static Method* org_apache_harmony_lang_annotation_AnnotationFactory_createAnnotation = NULL;
static Class* java_lang_annotation_Annotation = NULL;
static Class* array_of_java_lang_annotation_Annotation = NULL;
static ObjectArray* emptyExceptionTypes = NULL;
static ObjectArray* emptyAnnotations = NULL;

static Class* findType(Env* env, char* classDesc, ClassLoader* loader) {
    Class* c = nvmFindClassByDescriptor(env, classDesc, loader);
    if (!c) {
        if (nvmExceptionOccurred(env)->clazz == java_lang_ClassNotFoundException) {
            nvmExceptionClear(env);
            char* className = nvmCopyMemoryZ(env, classDesc);
            className[strlen(className)] = 0;
            nvmThrowNew(env, java_lang_TypeNotPresentException, nvmFromBinaryClassName(env, &className[1]));
        }
        return NULL;
    }
}

static jboolean throwFormatError(Env* env, char* expectedType) {
    char msg[64];
    snprintf(msg, sizeof(msg), "Invalid format: %s expected", expectedType);
    nvmThrowNew(env, java_lang_annotation_AnnotationFormatError, msg);
    return FALSE;
}

static inline jbyte getByte(void** attributes) {
    jbyte v = *(jbyte*) *attributes;
    *attributes += sizeof(jbyte);
    return v;
}

static inline jchar getChar(void** attributes) {
    jchar v = *(jchar*) *attributes;
    *attributes += sizeof(jchar);
    return v;
}

static inline jint getInt(void** attributes) {
    jint v = *(jint*) *attributes;
    *attributes += sizeof(jint);
    return v;
}

static inline jlong getLong(void** attributes) {
    jlong v = *(jlong*) *attributes;
    *attributes += sizeof(jlong);
    return v;
}

static inline jfloat getFloat(void** attributes) {
    jfloat v = *(jfloat*) *attributes;
    *attributes += sizeof(jfloat);
    return v;
}

static inline jdouble getDouble(void** attributes) {
    jdouble v = *(jdouble*) *attributes;
    *attributes += sizeof(jdouble);
    return v;
}

static inline char* getString(void** attributes) {
    char* v = *(char**) *attributes;
    *attributes += sizeof(char*);
    return v;
}

static void skipElementValue(void** attributes);

static void skipAnnotationElementValue(void** attributes) {
    jint length;
    getString(attributes); // Annotation class name
    length = getInt(attributes);
    while (length > 0) {
        getString(attributes); // Skip name
        skipElementValue(attributes);
        length--;
    }
}

static void skipElementValue(void** attributes) {
    jint length;

    jbyte tag = getByte(attributes);
    switch (tag) {
    case 'Z':
    case 'B':
    case 'S':
    case 'C':
    case 'I':
        getInt(attributes);
        break;
    case 'J':
        getLong(attributes);
        break;
    case 'F':
        getFloat(attributes);
        break;
    case 'D':
        getDouble(attributes);
        break;
    case 's':
        getString(attributes);
        break;
    case 'c':
        getString(attributes);
        break;
    case 'e':
        getString(attributes); // Enum class name
        getString(attributes); // Enum constant name
        break;
    case '[':
        length = getInt(attributes);
        while (length > 0) {
            skipElementValue(attributes);
            length--;
        }
        break;
    case '@':
        skipAnnotationElementValue(attributes);
        break;
    }
}

static void iterateAttributes(Env* env, void* attributes, jboolean (*f)(Env*, jbyte, void*, void*), void* data) {
    if (!attributes) return;

    jint length = 0;
    jint count = getInt(&attributes);

    while (count > 0) {
        jbyte type = getByte(&attributes);
        if (!f(env, type, attributes, data)) {
            return;
        }
        if (nvmExceptionCheck(env)) return;

        switch (type) {
        case SOURCE_FILE:
        case SIGNATURE:
            attributes += sizeof(char*);
            break;
        case INNER_CLASS:
            attributes += 3 * sizeof(char*) + sizeof(jint);
            break;
        case ENCLOSING_METHOD:
            attributes += 3 * sizeof(char*);
            break;
        case EXCEPTIONS:
            length = getInt(&attributes);
            attributes += length * sizeof(char*);
            break;
        case ANNOTATION_DEFAULT:
            skipElementValue(&attributes);
            break;
        case RUNTIME_VISIBLE_ANNOTATIONS:
            length = getInt(&attributes);
            while (length > 0) {
                skipAnnotationElementValue(&attributes);
                length--;
            }
            break;
        case RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS:
            // TODO: Implement
            count = 0;
            break;
        }
        count--;
    }
}

static jboolean parseElementValue(Env* env, void** attributes, Class* type, ClassLoader* classLoader, jvalue* result);

static jboolean parseBooleanElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'Z') return throwFormatError(env, "boolean");
    result->z = getInt(attributes);
    return TRUE;
}

static jboolean parseByteElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'B') return throwFormatError(env, "byte");
    result->b = getInt(attributes);
    return TRUE;
}

static jboolean parseShortElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'S') return throwFormatError(env, "short");
    result->s = getInt(attributes);
    return TRUE;
}

static jboolean parseCharElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'C') return throwFormatError(env, "char");
    result->c = getInt(attributes);
    return TRUE;
}

static jboolean parseIntElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'I') return throwFormatError(env, "int");
    result->i = getInt(attributes);
    return TRUE;
}

static jboolean parseLongElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'J') return throwFormatError(env, "long");
    result->j = getLong(attributes);
    return TRUE;
}

static jboolean parseFloatElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'F') return throwFormatError(env, "float");
    result->f = getFloat(attributes);
    return TRUE;
}

static jboolean parseDoubleElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'D') return throwFormatError(env, "double");
    result->d = getDouble(attributes);
    return TRUE;
}

static jboolean parseArrayElementValue(Env* env, void** attributes, Class* arrayClass, ClassLoader* classLoader, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != '[') return throwFormatError(env, "Array");

    // HACK: Possibly using nvmNewObjectArray() to create arrays of primitive types. It's ok as long as init (last) parameter is NULL.
    jint length = getChar(attributes);
    Array* array = (Array*) nvmNewObjectArray(env, length, NULL, arrayClass, NULL);
    if (!array) return FALSE;

    Class* componentType = nvmGetComponentType(env, arrayClass);
    jint i = 0;
    for (i = 0; i < length; i++) {
        jvalue v;
        if (!parseElementValue(env, attributes, componentType, classLoader, &v)) return FALSE;
        if (componentType->primitive) {
            switch (componentType->name[0]) {
            case 'Z':
                ((BooleanArray*) array)->values[i] = v.z;
                break;
            case 'B':
                ((ByteArray*) array)->values[i] = v.b;
                break;
            case 'S':
                ((ShortArray*) array)->values[i] = v.s;
                break;
            case 'C':
                ((CharArray*) array)->values[i] = v.c;
                break;
            case 'I':
                ((IntArray*) array)->values[i] = v.i;
                break;
            case 'J':
                ((LongArray*) array)->values[i] = v.j;
                break;
            case 'F':
                ((FloatArray*) array)->values[i] = v.f;
                break;
            case 'D':
                ((DoubleArray*) array)->values[i] = v.d;
                break;
            }
        } else {
            ((ObjectArray*) array)->values[i] = (Object*) v.l;
        }
    }
    result->l = (jobject) array;
    return result->l ? TRUE : FALSE;
}

static jboolean parseClassElementValue(Env* env, void** attributes, ClassLoader* classLoader, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'c') return throwFormatError(env, "java.lang.Class");
    char* className = getString(attributes);
    result->l = (jobject) findType(env, className, classLoader);
    return result->l ? TRUE : FALSE;
}

static jboolean parseStringElementValue(Env* env, void** attributes, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 's') return throwFormatError(env, "java.lang.String");
    char* s = getString(attributes);
    result->l = (jobject) nvmNewStringUTF(env, s, -1);
    return result->l ? TRUE : FALSE;
}

static jboolean parseEnumElementValue(Env* env, void** attributes, ClassLoader* classLoader, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != 'e') return throwFormatError(env, "java.lang.Enum");
    char* className = getString(attributes);
    char* constName = getString(attributes);
    Class* c = findType(env, className, classLoader);
    if (c) {
        ClassField* f = nvmGetClassField(env, c, constName, className);
        if (f) {
            result->l = (jobject) nvmGetObjectClassFieldValue(env, c, f);
        }
    }
    return result->l ? TRUE : FALSE;
}

static Method* getAnnotationValueMethod(Env* env, Class* clazz, char* name) {
    Method* method;
    for (method = clazz->methods->first; method != NULL; method = method->next) {
        if (!strcmp(method->name, name)) {
            return method;
        }
    }
    return NULL;
}

static jboolean getAnnotationValue(Env* env, void** attributes, Class* expectedAnnotationClass, ClassLoader* classLoader, jvalue* result) {
    char* annotationTypeName = getString(attributes);
    if (expectedAnnotationClass && strncmp(&annotationTypeName[1], expectedAnnotationClass->name, strlen(expectedAnnotationClass->name))) {
        return throwFormatError(env, nvmFromBinaryClassName(env, expectedAnnotationClass->name));
    }

    Class* annotationClass = expectedAnnotationClass;
    if (!annotationClass) {
        annotationClass = nvmFindClassByDescriptor(env, annotationTypeName, classLoader);
        if (!annotationClass) return FALSE;
    }

    jint length = getInt(attributes);

    ObjectArray* members = (ObjectArray*) nvmNewObjectArray(env, length, org_apache_harmony_lang_annotation_AnnotationMember, NULL, NULL);
    if (!members) return FALSE;

    jint i = 0;
    for (i = 0; i < length; i++) {
        char* name = getString(attributes);
        Method* method = getAnnotationValueMethod(env, annotationClass, name);
        if (!method) {
            skipElementValue(attributes);
        } else {
            Class* type = findType(env, nvmGetReturnType(method->desc), method->clazz->classLoader);
            Object* value = NULL;
            if (!type) {
                value = nvmExceptionClear(env);
            } else {
                jvalue v = {0};
                if (!parseElementValue(env, attributes, type, classLoader, &v)) {
                    value = nvmExceptionClear(env);
                } else {
                    value = nvmWrapPrimitive(env, type, &v);
                }
            }
            Object* jName = nvmNewStringUTF(env, name, -1);
            if (!jName) return FALSE;
            jvalue args[4];
            args[0].j = (jlong) method;
            Object* jMethod = nvmNewObjectA(env, java_lang_reflect_Method, java_lang_reflect_Method_init, args);
            if (!jMethod) return FALSE;

            args[0].l = (jobject) jName;
            args[1].l = (jobject) value;
            args[2].l = (jobject) type;;
            args[3].l = (jobject) jMethod;
            Object* member = nvmNewObjectA(env, org_apache_harmony_lang_annotation_AnnotationMember, 
                                           org_apache_harmony_lang_annotation_AnnotationMember_init, args);
            if (!member) return FALSE;
            members->values[i] = member;
        }
    }

    jvalue args[2];
    args[0].l = (jobject) annotationClass;
    args[1].l = (jobject) members;
    Object* o = nvmCallObjectClassMethodA(env, org_apache_harmony_lang_annotation_AnnotationFactory, 
                                          org_apache_harmony_lang_annotation_AnnotationFactory_createAnnotation, args);
    if (!nvmExceptionCheck(env)) result->l = (jobject) o;
    return result->l ? TRUE : FALSE;
}


static jboolean parseAnnotationElementValue(Env* env, void** attributes, Class* annotationClass, ClassLoader* classLoader, jvalue* result) {
    jbyte tag = getByte(attributes);
    if (tag != '@') return throwFormatError(env, "Annotation");

    return getAnnotationValue(env, attributes, annotationClass, classLoader, result);
}

static jboolean parseElementValue(Env* env, void** attributes, Class* type, ClassLoader* classLoader, jvalue* result) {
    if (type->primitive) {
        switch (type->name[0]) {
        case 'Z':
            return parseBooleanElementValue(env, attributes, result);
        case 'B':
            return parseByteElementValue(env, attributes, result);
        case 'S':
            return parseShortElementValue(env, attributes, result);
        case 'C':
            return parseCharElementValue(env, attributes, result);
        case 'I':
            return parseIntElementValue(env, attributes, result);
        case 'J':
            return parseLongElementValue(env, attributes, result);
        case 'F':
            return parseFloatElementValue(env, attributes, result);
        case 'D':
            return parseDoubleElementValue(env, attributes, result);
        }
    } else if (CLASS_IS_ARRAY(type)) {
        return parseArrayElementValue(env, attributes, type, classLoader, result);
    } else if (type == java_lang_Class) {
        return parseClassElementValue(env, attributes, classLoader, result);
    } else if (type == java_lang_String) {
        return parseStringElementValue(env, attributes, result);
    } else if (CLASS_IS_ENUM(type) && type->superclass == java_lang_Enum) {
        return parseEnumElementValue(env, attributes, classLoader, result);
    } else if (CLASS_IS_ANNOTATION(type) && CLASS_IS_INTERFACE(type)) {
        return parseAnnotationElementValue(env, attributes, type, classLoader, result);
    }

    return FALSE;
}

static jboolean innerClassesIterator(Env* env, jbyte type, void* attributes, void* data) {
    jboolean (*f)(Env*, char*, char*, char*, jint, void*) = ((void**) data)[0];
    void* fdata = ((void**) data)[1];
    if (type == INNER_CLASS) {
        char* innerClass = getString(&attributes);
        char* outerClass = getString(&attributes);
        char* innerName = getString(&attributes);
        jint access = getInt(&attributes);
        return f(env, innerClass, outerClass, innerName, access, fdata); // f decides whether we should stop iterating
    }
    return TRUE; // Continue with next attribute
}

static void iterateInnerClasses(Env* env, void* attributes, jboolean (*f)(Env*, char*, char*, char*, jint, void*), void* fdata) {
    void* data[2] = {f, fdata};
    iterateAttributes(env, attributes, innerClassesIterator, data);
}

static jboolean enclosingMethodsIterator(Env* env, jbyte type, void* attributes, void* data) {
    jboolean (*f)(Env*, char*, char*, char*, void*) = ((void**) data)[0];
    void* fdata = ((void**) data)[1];
    if (type == ENCLOSING_METHOD) {
        char* className = getString(&attributes);
        char* methodName = getString(&attributes);
        char* methodDesc = getString(&attributes);
        return f(env, className, methodName, methodDesc, fdata); // f decides whether we should stop iterating
    }
    return TRUE; // Continue with next attribute
}

static void iterateEnclosingMethods(Env* env, void* attributes, jboolean (*f)(Env*, char*, char*, char*, void*), void* fdata) {
    void* data[2] = {f, fdata};
    iterateAttributes(env, attributes, enclosingMethodsIterator, data);
}

static jboolean getDeclaringClassIterator(Env* env, char* innerClass, char* outerClass, char* innerName, jint access, void* data) {
    Class** result = (Class**) ((void**) data)[0];
    Class* clazz = (Class*) ((void**) data)[1];
    if (innerClass && outerClass && !strcmp(innerClass, clazz->name)) {
        *result = nvmFindClassUsingLoader(env, outerClass, clazz->classLoader);
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean getEnclosingClassIterator(Env* env, char* className, char* methodName, char* methodDesc, void* data) {
    Class** result = (Class**) ((void**) data)[0];
    Class* clazz = (Class*) ((void**) data)[1];
    *result = nvmFindClassUsingLoader(env, className, clazz->classLoader);
    return FALSE; // Stop iterating
}

static jboolean getEnclosingMethodIterator(Env* env, char* className, char* methodName, char* methodDesc, void* data) {
    Method** result = (Method**) ((void**) data)[0];
    Class* clazz = (Class*) ((void**) data)[1];
    if (methodName && methodDesc) {
        Class* c = nvmFindClassUsingLoader(env, className, clazz->classLoader);
        if (c) {
            *result = nvmGetMethod(env, c, methodName, methodDesc);
        }
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean isAnonymousClassIterator(Env* env, char* innerClass, char* outerClass, char* innerName, jint access, void* data) {
    jboolean* result = (jboolean*) ((void**) data)[0];
    Class* clazz = (Class*) ((void**) data)[1];
    if (innerClass && !strcmp(innerClass, clazz->name)) {
        *result = innerName == NULL ? TRUE : FALSE;
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean getSignatureIterator(Env* env, jbyte type, void* attributes, void* data) {
    Object** result = (Object**) data;
    if (type == SIGNATURE) {
        *result = nvmNewStringUTF(env, getString(&attributes), -1);
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean getExceptionsIterator(Env* env, jbyte type, void* attributes, void* data) {
    ObjectArray** result = (ObjectArray**) ((void**) data)[0];
    Method* method = (Method*) ((void**) data)[1];
    if (type == EXCEPTIONS) {
        jint length = getInt(&attributes);
        ObjectArray* array = nvmNewObjectArray(env, length, java_lang_Class, NULL, NULL);
        if (array) {
            jint i = 0;
            for (i = 0; i < length; i++) {
                char* className = getString(&attributes);
                Class* c = nvmFindClassUsingLoader(env, className, method->clazz->classLoader);
                if (!c) return FALSE;
                array->values[i] = (Object*) c;
            }
            *result = array;
        }
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean getAnnotationDefaultIterator(Env* env, jbyte type, void* attributes, void* data) {
    Object** result = (Object**) ((void**) data)[0];
    Method* method = (Method*) ((void**) data)[1];
    if (type == ANNOTATION_DEFAULT) {
        Class* type = findType(env, nvmGetReturnType(method->desc), method->clazz->classLoader);
        if (type) {
            jvalue value = {0};
            if (parseElementValue(env, &attributes, type, method->clazz->classLoader, &value)) {
                *result = nvmWrapPrimitive(env, type, &value);
            }
        }
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean getRuntimeVisibleAnnotationsIterator(Env* env, jbyte type, void* attributes, void* data) {
    ObjectArray** result = (ObjectArray**) ((void**) data)[0];
    ClassLoader* classLoader = (ClassLoader*) ((void**) data)[1];
    if (type == RUNTIME_VISIBLE_ANNOTATIONS) {
        jint length = getInt(&attributes);
        ObjectArray* annotations = nvmNewObjectArray(env, length, java_lang_annotation_Annotation, NULL, NULL);
        if (!annotations) return FALSE;
        jint i = 0;
        for (i = 0; i < length; i++) {
            jvalue value = {0};
            if (!getAnnotationValue(env, &attributes, NULL, classLoader, &value)) return FALSE;
            annotations->values[i] = (Object*) value.l;
        }
        *result = annotations;
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean getRuntimeVisibleParameterAnnotationsIterator(Env* env, jbyte type, void* attributes, void* data) {
    ObjectArray** result = (ObjectArray**) ((void**) data)[0];
    ClassLoader* classLoader = (ClassLoader*) ((void**) data)[1];
    if (type == RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS) {
        jint numParams = getInt(&attributes);
        ObjectArray* paramAnnotations = nvmNewObjectArray(env, numParams, array_of_java_lang_annotation_Annotation, NULL, NULL);
        if (!paramAnnotations) return FALSE;
        jint i = 0;
        for (i = 0; i < numParams; i++) {
            jint length = getInt(&attributes);
            ObjectArray* annotations = nvmNewObjectArray(env, length, java_lang_annotation_Annotation, NULL, NULL);
            if (!annotations) return FALSE;
            jint j = 0;
            for (j = 0; j < length; j++) {
                jvalue value = {0};
                if (!getAnnotationValue(env, &attributes, NULL, classLoader, &value)) return FALSE;
                annotations->values[j] = (Object*) value.l;
            }
            paramAnnotations->values[i] = (Object*) annotations;
        }
        *result = paramAnnotations;
        return FALSE; // Stop iterating
    }
    return TRUE; // Continue with next attribute
}

static jboolean getDeclaredClassesCountIterator(Env* env, char* innerClass, char* outerClass, char* innerName, jint access, void* data) {
    jint* result = (jint*) ((void**) data)[0];
    Class* clazz = (Class*) ((void**) data)[1];
    if (!outerClass || strcmp(outerClass, clazz->name)) {
        return TRUE; // Continue with next attribute
    }
    *result = *result + 1;
    return TRUE; // Continue with next attribute
}

static jboolean getDeclaredClassesIterator(Env* env, char* innerClass, char* outerClass, char* innerName, jint access, void* data) {
    ObjectArray* result = (ObjectArray*) ((void**) data)[0];
    jint* index = (jint*) ((void**) data)[1];
    Class* clazz = (Class*) ((void**) data)[2];
    if (!outerClass || strcmp(outerClass, clazz->name)) {
        return TRUE; // Continue with next attribute
    }
    Class* c = nvmFindClassUsingLoader(env, innerClass, clazz->classLoader);
    if (!c) return FALSE; // Stop iterating
    result->values[*index] = (Object*) c;
    *index = *index + 1;
    return TRUE; // Continue with next attribute
}

jboolean nvmInitAttributes(Env* env) {
    java_lang_TypeNotPresentException = nvmFindClassUsingLoader(env, "java/lang/TypeNotPresentException", NULL);
    if (!java_lang_TypeNotPresentException) return FALSE;
    java_lang_annotation_AnnotationFormatError = nvmFindClassUsingLoader(env, "java/lang/annotation/AnnotationFormatError", NULL);
    if (!java_lang_annotation_AnnotationFormatError) return FALSE;
    java_lang_reflect_Method = nvmFindClassUsingLoader(env, "java/lang/reflect/Method", NULL);
    if (!java_lang_reflect_Method) return FALSE;
    java_lang_reflect_Method_init = nvmGetInstanceMethod(env, java_lang_reflect_Method, "<init>", "(J)V");
    if (!java_lang_reflect_Method_init) return FALSE;
    org_apache_harmony_lang_annotation_AnnotationMember = nvmFindClassUsingLoader(env, "org/apache/harmony/lang/annotation/AnnotationMember", NULL);
    if (!org_apache_harmony_lang_annotation_AnnotationMember) return FALSE;
    org_apache_harmony_lang_annotation_AnnotationMember_init = nvmGetInstanceMethod(env, org_apache_harmony_lang_annotation_AnnotationMember, 
        "<init>", "(Ljava/lang/String;Ljava/lang/Object;Ljava/lang/Class;Ljava/lang/reflect/Method;)V");
    if (!org_apache_harmony_lang_annotation_AnnotationMember_init) return FALSE;

    org_apache_harmony_lang_annotation_AnnotationFactory = nvmFindClassUsingLoader(env, "org/apache/harmony/lang/annotation/AnnotationFactory", NULL);
    if (!org_apache_harmony_lang_annotation_AnnotationFactory) return FALSE;
    org_apache_harmony_lang_annotation_AnnotationFactory_createAnnotation = nvmGetClassMethod(env, org_apache_harmony_lang_annotation_AnnotationFactory, 
        "createAnnotation", "(Ljava/lang/Class;[Lorg/apache/harmony/lang/annotation/AnnotationMember;)Ljava/lang/annotation/Annotation;");
    if (!org_apache_harmony_lang_annotation_AnnotationFactory_createAnnotation) return FALSE;

    java_lang_annotation_Annotation = nvmFindClassUsingLoader(env, "java/lang/annotation/Annotation", NULL);
    if (!java_lang_annotation_Annotation) return FALSE;
    array_of_java_lang_annotation_Annotation = nvmFindClassUsingLoader(env, "[Ljava/lang/annotation/Annotation;", NULL);
    if (!array_of_java_lang_annotation_Annotation) return FALSE;

    emptyExceptionTypes = nvmNewObjectArray(env, 0, java_lang_Class, NULL, NULL);
    if (!emptyExceptionTypes) return FALSE;

    emptyAnnotations = nvmNewObjectArray(env, 0, java_lang_annotation_Annotation, NULL, NULL);
    if (!emptyAnnotations) return FALSE;

    return TRUE;
}

Class* nvmAttributeGetDeclaringClass(Env* env, Class* clazz) {
    Class* result = NULL;
    void* data[2] = {&result, clazz};
    iterateInnerClasses(env, clazz->attributes, getDeclaringClassIterator, data);
    return result;
}

Class* nvmAttributeGetEnclosingClass(Env* env, Class* clazz) {
    Class* result = NULL;
    void* data[2] = {&result, clazz};
    iterateEnclosingMethods(env, clazz->attributes, getEnclosingClassIterator, data);
    return result;
}

Method* nvmAttributeGetEnclosingMethod(Env* env, Class* clazz) {
    Method* result = NULL;
    void* data[2] = {&result, clazz};
    iterateEnclosingMethods(env, clazz->attributes, getEnclosingMethodIterator, data);
    return result;
}

jboolean nvmAttributeIsAnonymousClass(Env* env, Class* clazz) {
    jboolean result = FALSE;
    void* data[2] = {&result, clazz};
    iterateInnerClasses(env, clazz->attributes, isAnonymousClassIterator, data);
    return result;
}

Object* nvmAttributeGetClassSignature(Env* env, Class* clazz) {
    Object* result = NULL;
    iterateAttributes(env, clazz->attributes, getSignatureIterator, &result);
    return result;
}

Object* nvmAttributeGetMethodSignature(Env* env, Method* method) {
    Object* result = NULL;
    iterateAttributes(env, method->attributes, getSignatureIterator, &result);
    return result;
}

Object* nvmAttributeGetFieldSignature(Env* env, Field* field) {
    Object* result = NULL;
    iterateAttributes(env, field->attributes, getSignatureIterator, &result);
    return result;
}

ObjectArray* nvmAttributeGetExceptions(Env* env, Method* method) {
    if (!method->attributes) return emptyExceptionTypes;
    ObjectArray* result = NULL;
    void* data[2] = {&result, method};
    iterateAttributes(env, method->attributes, getExceptionsIterator, data);
    return result ? result : emptyExceptionTypes;
}

Object* nvmAttributeGetAnnotationDefault(Env* env, Method* method) {
    Object* result = NULL;
    void* data[2] = {&result, method};
    iterateAttributes(env, method->attributes, getAnnotationDefaultIterator, data);
    return result;
}

ObjectArray* nvmAttributeGetClassRuntimeVisibleAnnotations(Env* env, Class* clazz) {
    ObjectArray* result = NULL;
    void* data[2] = {&result, clazz->classLoader};
    iterateAttributes(env, clazz->attributes, getRuntimeVisibleAnnotationsIterator, data);
    return result ? result : emptyAnnotations;
}

ObjectArray* nvmAttributeGetMethodRuntimeVisibleAnnotations(Env* env, Method* method) {
    ObjectArray* result = NULL;
    void* data[2] = {&result, method->clazz->classLoader};
    iterateAttributes(env, method->attributes, getRuntimeVisibleAnnotationsIterator, data);
    return result ? result : emptyAnnotations;
}

ObjectArray* nvmAttributeGetFieldRuntimeVisibleAnnotations(Env* env, Field* field) {
    ObjectArray* result = NULL;
    void* data[2] = {&result, field->clazz->classLoader};
    iterateAttributes(env, field->attributes, getRuntimeVisibleAnnotationsIterator, data);
    return result ? result : emptyAnnotations;
}

ObjectArray* nvmAttributeGetMethodRuntimeVisibleParameterAnnotations(Env* env, Method* method) {
    ObjectArray* result = NULL;
    void* data[2] = {&result, method->clazz->classLoader};
    iterateAttributes(env, method->attributes, getRuntimeVisibleParameterAnnotationsIterator, data);
    return result ? result : emptyAnnotations;
}

ObjectArray* nvmAttributeGetDeclaredClasses(Env* env, Class* clazz) {
    if (!clazz->attributes) return NULL;
    jint count = 0;
    void* countData[2] = {&count, clazz};
    iterateInnerClasses(env, clazz->attributes, getDeclaredClassesCountIterator, countData);
    if (count == 0) return NULL;
    ObjectArray* result = nvmNewObjectArray(env, count, java_lang_Class, NULL, NULL);
    jint index = 0;
    void* data[3] = {result, &index, clazz};
    iterateInnerClasses(env, clazz->attributes, getDeclaredClassesIterator, data);
    return result;
}
