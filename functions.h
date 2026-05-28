#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <stdlib.h>
#include "sapnwrfc.h"

class CFunctions {

	public:
		static int listening;
		static int on_agree_click(void);
		static void read_value(SAP_UC* buffer, int max);

		static void listen(RFC_CONNECTION_HANDLE* pserver_handle, const RFC_CONNECTION_PARAMETER* params);
		static void check_for_reset(RFC_CONNECTION_HANDLE conn);
		static void print_exports(RFC_FUNCTION_DESC_HANDLE description, RFC_FUNCTION_HANDLE container);
		static void fill_imports(RFC_FUNCTION_DESC_HANDLE description, RFC_FUNCTION_HANDLE container);
		static void error_throwing(RFC_FUNCTION_DESC_HANDLE metadata, RFC_ERROR_INFO* error_info);
		static void fill_sy_msg(RFC_ERROR_INFO* perror_info);
		static void print_imports(RFC_FUNCTION_DESC_HANDLE description, RFC_FUNCTION_HANDLE container);
		static void fill_exports(RFC_FUNCTION_DESC_HANDLE description, RFC_FUNCTION_HANDLE container);

		void fill_table(unsigned indent, RFC_TYPE_DESC_HANDLE type_desc, RFC_TABLE_HANDLE container);
		void fill_structure(unsigned indent, RFC_TYPE_DESC_HANDLE type_desc, RFC_STRUCTURE_HANDLE container);
		void print_table(unsigned indent, RFC_TYPE_DESC_HANDLE type_desc, RFC_TABLE_HANDLE container);
		void print_structure(unsigned indent, RFC_TYPE_DESC_HANDLE type_desc, RFC_STRUCTURE_HANDLE container);

		static RFC_RC check_authorization(RFC_ABAP_NAME func_name, RFC_ATTRIBUTES attributes);
		static RFC_RC SAP_API generic_request_handler(RFC_CONNECTION_HANDLE rfc_handle, RFC_FUNCTION_HANDLE func_handle, RFC_ERROR_INFO* error_info);
		static RFC_RC SAP_API repository_lookup(SAP_UC const* function_name, RFC_ATTRIBUTES rfc_attributes, RFC_FUNCTION_DESC_HANDLE* func_desc_handle);

		static void fill_parameter(int* index, RFC_PARAMETER_DESC desc, RFC_FUNCTION_HANDLE container) {}
		static void print_parameter(RFC_PARAMETER_DESC param_desc, RFC_FUNCTION_HANDLE container) {}
};

#endif // FUNCTIONS_H
