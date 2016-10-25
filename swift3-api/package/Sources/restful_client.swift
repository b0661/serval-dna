/*
Serval DNA Swift API
Copyright (C) 2016 Flinders University

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

import Foundation

public class ServalRestfulClient {

    public struct Configuration {
        public let host : String
        public let port : Int16
        public let username : String
        public let password : String

        public func with(host: String? = nil, port: Int16? = nil, username: String? = nil, password: String? = nil) -> Configuration {
            return Configuration(host: host ?? type(of:self).default.host,
                                 port: port ?? type(of:self).default.port,
                                 username: username ?? type(of:self).default.username,
                                 password: password ?? type(of:self).default.password)
        }

        public static let `default` = Configuration(
            host: "127.0.0.1",
            port : 4110,
            username: "",
            password: ""
        )
    }

    public let configuration : Configuration

    public enum Exception : Error {
        case requestFailed(statusCode: Int)
        case missingContentType
        case invalidContentType(mimeType: String)
        case malformedJson
        case invalidJson(reason: String)
    }

    public struct Request {
        fileprivate let urlSession : URLSession
        fileprivate let dataTask : URLSessionDataTask
        public func close() {
            print("close") // TODO
        }
    }

    public init(configuration: Configuration = Configuration.default) {
        self.configuration = configuration
    }

    public func createRequest(path: String,
                              query: [String: String] = [:],
                              completionHandler: @escaping (Int?, Any?, Error?) -> Void)
        -> Request?
    {
        var urlComponents = URLComponents()
        urlComponents.scheme = "http"
        urlComponents.host = self.configuration.host
        urlComponents.port = Int(self.configuration.port)
        urlComponents.path = "/\(path)"
        if !query.isEmpty {
            var items : [URLQueryItem] = []
            for (name, value) in query {
                items.append(URLQueryItem(name: name, value: value))
            }
            urlComponents.queryItems = items
            // Force re-computation of urlComponents.url
            guard urlComponents.percentEncodedQuery != nil else {
                return nil
            }
        }
        guard let url = urlComponents.url else {
            return nil
        }
        let sessionConfiguration = URLSessionConfiguration.default
#if !os(Linux)
        // At November 2016, On Linux, the following lines produce the run-time
        // error:
        //   Could not cast value of type 'Swift.AnyHashable' to 'Foundation.NSString'
        // but they work perfectly on OS X.  Once this is fixed, the
        // conditional around this code can be eliminated.  Until then, this
        // request will only work on Linux if the Serval DNA daemon has been
        // configured with api.restful.authorization=noauth
        let userpass = self.configuration.username + ":" + self.configuration.password
        sessionConfiguration.httpAdditionalHeaders = ["Authorization": "Basic " + Data(userpass.utf8).base64EncodedString()]
#endif
        let urlSession = URLSession(configuration: sessionConfiguration)
        print(url)
        let dataTask = urlSession.dataTask(with: url) { (data, response, error) in
            if let error = error {
                completionHandler(nil, nil, error)
                return
            }
            let httpResponse = response as! HTTPURLResponse
            guard let mimeType = httpResponse.mimeType else {
                completionHandler(nil, nil, ServalRestfulClient.Exception.missingContentType)
                return
            }
            guard mimeType == "application/json" else {
                completionHandler(nil, nil, ServalRestfulClient.Exception.invalidContentType(mimeType: mimeType))
                return
            }
            guard let json = try? JSONSerialization.jsonObject(with: data!, options: []) else {
                completionHandler(nil, nil, ServalRestfulClient.Exception.malformedJson)
                return
            }
            completionHandler(httpResponse.statusCode, json, nil)
        }
        dataTask.resume()
        return Request(urlSession: urlSession, dataTask: dataTask)
    }
}
