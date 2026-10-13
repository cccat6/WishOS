// Example host-side application service. Install your own service module with
// NOTES_SERVICE_MODULE; protocol drivers contain no ecosystem/application logic.
export const handlers={
  transform: async request=>({request_id:request.request_id,value:String(request.payload.value??'').toUpperCase()})
};
