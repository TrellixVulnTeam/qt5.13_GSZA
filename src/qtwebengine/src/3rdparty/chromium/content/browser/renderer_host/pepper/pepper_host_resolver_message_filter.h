// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_HOST_RESOLVER_MESSAGE_FILTER_H_
#define CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_HOST_RESOLVER_MESSAGE_FILTER_H_

#include <stdint.h>

#include <string>
#include <vector>

#include "base/compiler_specific.h"
#include "base/macros.h"
#include "content/common/content_export.h"
#include "content/public/common/process_type.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "ppapi/c/pp_instance.h"
#include "ppapi/host/resource_message_filter.h"
#include "services/network/public/mojom/network_context.mojom.h"

struct PP_HostResolver_Private_Hint;
struct PP_NetAddress_Private;

namespace net {
class AddressList;
}

namespace ppapi {
struct HostPortPair;

namespace host {
struct HostMessageContext;
}
}

namespace content {

class BrowserPpapiHostImpl;

class CONTENT_EXPORT PepperHostResolverMessageFilter
    : public ppapi::host::ResourceMessageFilter,
      public network::mojom::ResolveHostClient {
 public:
  PepperHostResolverMessageFilter(BrowserPpapiHostImpl* host,
                                  PP_Instance instance,
                                  bool private_api);

 protected:
  ~PepperHostResolverMessageFilter() override;

 private:
  typedef std::vector<PP_NetAddress_Private> NetAddressList;

  // ppapi::host::ResourceMessageFilter overrides.
  scoped_refptr<base::TaskRunner> OverrideTaskRunnerForMessage(
      const IPC::Message& message) override;
  int32_t OnResourceMessageReceived(
      const IPC::Message& msg,
      ppapi::host::HostMessageContext* context) override;

  int32_t OnMsgResolve(const ppapi::host::HostMessageContext* context,
                       const ppapi::HostPortPair& host_port,
                       const PP_HostResolver_Private_Hint& hint);

  // network::mojom::ResolveHostClient overrides.
  void OnComplete(
      int result,
      const base::Optional<net::AddressList>& resolved_addresses) override;

  void OnLookupFinished(int net_result,
                        const base::Optional<net::AddressList>& addresses,
                        const ppapi::host::ReplyMessageContext& bound_info);
  void SendResolveReply(int32_t result,
                        const std::string& canonical_name,
                        const NetAddressList& net_address_list,
                        const ppapi::host::ReplyMessageContext& context);
  void SendResolveError(int32_t error,
                        const ppapi::host::ReplyMessageContext& context);

  // The following members are only accessed on the IO thread.

  bool external_plugin_;
  bool private_api_;
  int render_process_id_;
  int render_frame_id_;

  // The following members are only accessed on the UI thread.

  // A reference to |this| must always be taken while |binding_| is bound to
  // ensure that if the error callback is called the object is alive.
  mojo::Binding<network::mojom::ResolveHostClient> binding_;

  ppapi::host::ReplyMessageContext host_resolve_context_;

  DISALLOW_COPY_AND_ASSIGN(PepperHostResolverMessageFilter);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_PEPPER_PEPPER_HOST_RESOLVER_MESSAGE_FILTER_H_